/*
 * Copyright (c) 2017-2019 The University of Manchester
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file
 *
 *  \brief This file contains the main functions for a poisson spike generator.
 *
 *
 */

#include <common/maths-util.h>

#include <data_specification.h>
#include <recording.h>
#include <debug.h>
#include <random.h>
#include <simulation.h>
#include <spin1_api.h>
#include <bit_field.h>
#include <stdfix-full-iso.h>
#include <limits.h>

#include "profile_tags.h"
#include <profiler.h>

// Declare spin1_wfi
extern void spin1_wfi();

// Spin1 API ticks - to know when the timer wraps
extern uint ticks;

//! data structure for poisson sources
typedef struct spike_source_t {
    uint32_t start_ticks;
    uint32_t end_ticks;
    bool is_fast_source;

    UFRACT exp_minus_lambda;
    REAL sqrt_lambda;
    uint32_t mean_isi_ticks;
    uint32_t time_to_spike_ticks;
} spike_source_t;

//! \brief data structure for recording spikes
typedef struct timed_out_spikes{
    uint32_t time;
    uint32_t n_buffers;
    uint32_t out_spikes[];
} timed_out_spikes;

//! spike source array region IDs in human readable form
typedef enum region {
    SYSTEM, POISSON_PARAMS,
    SPIKE_HISTORY_REGION,
    PROVENANCE_REGION,
    PROFILER_REGION
} region;

#define NUMBER_OF_REGIONS_TO_RECORD 1
#define BYTE_TO_WORD_CONVERTER 4
//! A scale factor to allow the use of integers for "inter-spike intervals"
#define ISI_SCALE_FACTOR 1000

typedef enum callback_priorities{
    MULTICAST = -1, SDP = 0, TIMER = 2, DMA = 1
} callback_priorities;

//! Parameters of the SpikeSourcePoisson
struct global_parameters {

    //! True if there is a key to transmit, False otherwise
    bool has_key;

    //! The base key to send with (neuron ID to be added to it), or 0 if no key
    uint32_t key;

    //! The mask to work out the neuron ID when setting the rate
    uint32_t set_rate_neuron_id_mask;

    //! The offset of the timer ticks to desynchronize sources
    uint32_t timer_offset;

    //! The expected time to wait between spikes
    uint32_t time_between_spikes;

    //! The time between ticks in seconds for setting the rate
    UFRACT seconds_per_tick;

    //! The number of ticks per second for setting the rate
    uint32_t ticks_per_second;

    //! The border rate between slow and fast sources
    REAL slow_rate_per_tick_cutoff;

    //! The border rate between fast and faster sources
    REAL fast_rate_per_tick_cutoff;

    //! The ID of the first source relative to the population as a whole
    uint32_t first_source_id;

    //! The number of sources in this sub-population
    uint32_t n_spike_sources;

    //! The seed for the Poisson generation process
    mars_kiss64_seed_t spike_source_seed;
};

//! The global_parameters for the sub-population
static struct global_parameters global_parameters;

//! global variable which contains all the data for neurons
static spike_source_t *poisson_parameters = NULL;

//! The expected current clock tick of timer_1
static uint32_t expected_time;

//! keeps track of which types of recording should be done to this model.
static uint32_t recording_flags = 0;

//! the time interval parameter TODO this variable could be removed and use the
//! timer tick callback timer value.
static uint32_t time;

//! the number of timer ticks that this model should run for before exiting.
static uint32_t simulation_ticks = 0;

//! the int that represents the bool for if the run is infinite or not.
static uint32_t infinite_run;

//! The recorded spikes
static timed_out_spikes *spikes = NULL;

//! The number of recording spike buffers that have been allocated
static uint32_t n_spike_buffers_allocated;

//! The number of words needed for 1 bit per source
static uint32_t n_spike_buffer_words;

//! The size of each spike buffer in bytes
static uint32_t spike_buffer_size;

//! True if DMA recording is currently in progress
static bool recording_in_progress = false;

//! The timer period
static uint32_t timer_period;

//! \brief Set specific spikes for recording
//! \param[in] n is the spike array index
//! \return bit field at the location n
static inline bit_field_t _out_spikes(uint32_t n) {
    return &(spikes->out_spikes[n * n_spike_buffer_words]);
}

//! \brief Reset the spike buffer by clearing the bit field
//! \return None
static inline void _reset_spikes() {
    spikes->n_buffers = 0;
    for (uint32_t n = n_spike_buffers_allocated; n > 0; n--) {
        clear_bit_field(_out_spikes(n - 1), n_spike_buffer_words);
    }
}

//! \brief deduces the time in timer ticks multiplied by ISI_SCALE_FACTOR
//!        until the next spike is to occur given the mean inter-spike interval
//! \param[in] mean_inter_spike_interval_in_ticks The mean number of ticks
//!            before a spike is expected to occur in a slow process.
//! \return a uint32_t which represents "time" in timer ticks * ISI_SCALE_FACTOR
//!         until the next spike occurs
static inline uint32_t slow_spike_source_get_time_to_spike(
        uint32_t mean_inter_spike_interval_in_ticks) {
    // Round (dist variate * ISI_SCALE_FACTOR), convert to uint32
    int nbits = 15;
    uint32_t value = (uint32_t) roundk(exponential_dist_variate(
            mars_kiss64_seed, global_parameters.spike_source_seed) * ISI_SCALE_FACTOR, nbits);
    // Now multiply by the mean ISI
    uint32_t exp_variate = value * mean_inter_spike_interval_in_ticks;
    // Note that this will be compared to ISI_SCALE_FACTOR in the main loop!
    return exp_variate;
}

//! \brief Determines how many spikes to transmit this timer tick, for a fast source
//! \param[in] exp_minus_lambda exp(-lambda), lambda is amount of spikes expected to be
//!            produced this timer interval (timer tick in real time)
//! \return a uint32_t which represents the number of spikes to transmit
//!         this timer tick
static inline uint32_t fast_spike_source_get_num_spikes(
        UFRACT exp_minus_lambda) {
    // If the value of exp_minus_lambda is very small then it's not worth
    // using the algorithm, so just return 0
    if (bitsulr(exp_minus_lambda) == bitsulr(UFRACT_CONST(0.0))) {
        return 0;
    }
    else {
        return poisson_dist_variate_exp_minus_lambda(
            mars_kiss64_seed,
            global_parameters.spike_source_seed, exp_minus_lambda);
    }
}

//! \brief Determines how many spikes to transmit this timer tick, for a faster source
//!        (where lambda is large enough that a Gaussian can be used instead of a Poisson)
//! \param[in] sqrt_lambda Square root of the amount of spikes expected to be produced
//!            this timer interval (timer tick in real time)
//! \return a uint32_t which represents the number of spikes to transmit
//!         this timer tick
static inline uint32_t faster_spike_source_get_num_spikes(
        REAL sqrt_lambda) {
    // First we do x = (invgausscdf(U(0,1)) * 0.5) + sqrt(lambda)
    REAL x = (gaussian_dist_variate(
            mars_kiss64_seed,
            global_parameters.spike_source_seed) * REAL_CONST(0.5)) + sqrt_lambda;
    // Then we return int(roundk(x^2))
    int nbits = 15;
    return (uint32_t) roundk(x * x, nbits);
}

void print_spike_sources(){
    for (index_t s = 0; s < global_parameters.n_spike_sources; s++) {
        log_info("atom %d", s);
        log_info("scaled_start = %u", poisson_parameters[s].start_ticks);
        log_info("scaled end = %u", poisson_parameters[s].end_ticks);
        log_info("is_fast_source = %d", poisson_parameters[s].is_fast_source);
        log_info(
            "exp_minus_lambda = %k",
            (REAL)(poisson_parameters[s].exp_minus_lambda));
        log_info("sqrt_lambda = %k", poisson_parameters[s].sqrt_lambda);
        log_info("isi_val = %u", poisson_parameters[s].mean_isi_ticks);
        log_info(
            "time_to_spike = %u", poisson_parameters[s].time_to_spike_ticks);
    }
}

//! \brief entry method for reading the global parameters stored in Poisson
//!        parameter region
//! \param[in] address the absolute SDRAm memory address to which the
//!            Poisson parameter region starts.
//! \return a boolean which is True if the parameters were read successfully or
//!         False otherwise
bool read_global_parameters(address_t address) {

    log_info("read global_parameters: starting");

    spin1_memcpy(&global_parameters, address, sizeof(global_parameters));

    log_info(
        "\t key = %08x, set rate mask = %08x, timer offset = %u",
        global_parameters.key, global_parameters.set_rate_neuron_id_mask,
        global_parameters.timer_offset);

    log_info("\t seed = %u %u %u %u", global_parameters.spike_source_seed[0],
        global_parameters.spike_source_seed[1],
        global_parameters.spike_source_seed[2],
        global_parameters.spike_source_seed[3]);

    validate_mars_kiss64_seed(global_parameters.spike_source_seed);

    log_info(
        "\t spike sources = %u, starting at %u",
        global_parameters.n_spike_sources, global_parameters.first_source_id);
    log_info(
        "seconds_per_tick = %k\n",
        (REAL)(global_parameters.seconds_per_tick));
    log_info("ticks_per_second = %u\n", global_parameters.ticks_per_second);
    log_info(
        "slow_rate_per_tick_cutoff = %k\n",
        global_parameters.slow_rate_per_tick_cutoff);
    log_info(
        "fast_rate_per_tick_cutoff = %k\n",
        global_parameters.fast_rate_per_tick_cutoff);

    log_info("read_global_parameters: completed successfully");
    return true;
}

//! \brief method for reading the parameters stored in Poisson parameter region
//! \param[in] address the absolute SDRAm memory address to which the
//!            Poisson parameter region starts.
//! \return a boolean which is True if the parameters were read successfully or
//!         False otherwise
static bool read_poisson_parameters(address_t address) {

    // Allocate DTCM for array of spike sources and copy block of data
    if (global_parameters.n_spike_sources > 0) {

        // the first time around, the array is set to NULL, afterwards,
        // assuming all goes well, there's an address here.
        if (poisson_parameters == NULL){
            poisson_parameters = (spike_source_t*) spin1_malloc(
                global_parameters.n_spike_sources * sizeof(spike_source_t));
        }

        // if failed to alloc memory, report and fail.
        if (poisson_parameters == NULL) {
            log_error("Failed to allocate poisson_parameters");
            return false;
        }


        // store spike source data into DTCM
        uint32_t spikes_offset = sizeof(global_parameters) / 4;
        spin1_memcpy(
            poisson_parameters, &address[spikes_offset],
            global_parameters.n_spike_sources * sizeof(spike_source_t));
    }
    log_info("read_poisson_parameters: completed successfully");
    return true;
}

//! \brief Initialises the recording parts of the model
//! \return True if recording initialisation is successful, false otherwise
static bool initialise_recording(){

    // Get the address this core's DTCM data starts at from SRAM
    data_specification_metadata_t *ds_regions =
            data_specification_get_data_address();

    // Get the system region
    address_t recording_region = data_specification_get_region(
            SPIKE_HISTORY_REGION, ds_regions);

    bool success = recording_initialize(recording_region, &recording_flags);
    log_info("Recording flags = 0x%08x", recording_flags);

    return success;
}

//! Initialises the model by reading in the regions and checking recording
//! data.
//! \param[out] timer_period a pointer for the memory address where the timer
//!            period should be stored during the function.
//! \param[out] update_sdp_port The SDP port on which to listen for rate
//!             updates
//! \return boolean of True if it successfully read all the regions and set up
//!         all its internal data structures. Otherwise returns False
static bool initialize() {
    log_info("Initialise: started");

    // Get the address this core's DTCM data starts at from SRAM
    data_specification_metadata_t *ds_regions =
            data_specification_get_data_address();

    // Read the header
    if (!data_specification_read_header(ds_regions)) {
        return false;
    }

    // Get the timing details and set up the simulation interface
    if (!simulation_initialise(
            data_specification_get_region(SYSTEM, ds_regions),
            APPLICATION_NAME_HASH, &timer_period, &simulation_ticks,
            &infinite_run, &time, SDP, DMA)) {
        return false;
    }
    simulation_set_provenance_data_address(
            data_specification_get_region(PROVENANCE_REGION, ds_regions));

    // setup recording region
    if (!initialise_recording()){
        return false;
    }

    // Setup regions that specify spike source array data
    if (!read_global_parameters(
            data_specification_get_region(POISSON_PARAMS, ds_regions))) {
        return false;
    }

    if (!read_poisson_parameters(
            data_specification_get_region(POISSON_PARAMS, ds_regions))) {
        return false;
    }

    // Loop through slow spike sources and initialise 1st time to spike
    for (index_t s = 0; s < global_parameters.n_spike_sources; s++) {
        if (!poisson_parameters[s].is_fast_source) {
            poisson_parameters[s].time_to_spike_ticks =
                slow_spike_source_get_time_to_spike(
                    poisson_parameters[s].mean_isi_ticks);
        }
    }

    // print spike sources for debug purposes
    // print_spike_sources();

    // Set up recording buffer
    n_spike_buffers_allocated = 0;
    n_spike_buffer_words = get_bit_field_size(
        global_parameters.n_spike_sources);
    spike_buffer_size = n_spike_buffer_words * sizeof(uint32_t);

    // Setup profiler
    profiler_init(
        data_specification_get_region(PROFILER_REGION, ds_regions));

    log_info("Initialise: completed successfully");

    return true;
}

//! \brief runs any functions needed at resume time.
//! \return None
void resume_callback() {
    recording_reset();

    data_specification_metadata_t *ds_regions =
            data_specification_get_data_address();

    if (!read_poisson_parameters(
            data_specification_get_region(POISSON_PARAMS, ds_regions))){
        log_error("failed to reread the Poisson parameters from SDRAM");
        rt_error(RTE_SWERR);
    }

    // Loop through slow spike sources and initialise 1st time to spike
    for (index_t s = 0; s < global_parameters.n_spike_sources; s++) {
        if (!poisson_parameters[s].is_fast_source &&
                poisson_parameters[s].time_to_spike_ticks == 0) {
            poisson_parameters[s].time_to_spike_ticks =
                slow_spike_source_get_time_to_spike(
                    poisson_parameters[s].mean_isi_ticks);
        }
    }

    log_info("Successfully resumed Poisson spike source at time: %u", time);

    // print spike sources for debug purposes
    // print_spike_sources();
}

//! \brief stores the Poisson parameters back into SDRAM for reading by the
//! host when needed
//! \return None
bool store_poisson_parameters() {
    log_info("stored_parameters: starting");

    // Get the address this core's DTCM data starts at from SRAM
    data_specification_metadata_t *ds_regions =
            data_specification_get_data_address();
    address_t param_store =
            data_specification_get_region(POISSON_PARAMS, ds_regions);

    // Copy the global_parameters back to SDRAM
    spin1_memcpy(param_store, &global_parameters, sizeof(global_parameters));

    // store spike source parameters into array into SDRAM for reading by
    // the host
    if (global_parameters.n_spike_sources > 0) {
        uint32_t spikes_offset =
                sizeof(global_parameters) / BYTE_TO_WORD_CONVERTER;
        spin1_memcpy(
                &param_store[spikes_offset], poisson_parameters,
                global_parameters.n_spike_sources * sizeof(spike_source_t));
    }

    log_info("stored_parameters : completed successfully");
    return true;
}

//! \brief handles spreading of Poisson spikes for even packet reception at
//! destination
//! \param[in] spike_key: the key to transmit
//! \return None
void _send_spike(uint spike_key, uint timer_count) {

    // Wait until the expected time to send
    while ((ticks == timer_count) && (tc[T1_COUNT] > expected_time)) {

        // Do Nothing
    }
    expected_time -= global_parameters.time_between_spikes;

    // Send the spike
    log_debug("Sending spike packet %x at %d\n", spike_key, time);
    while (!spin1_send_mc_packet(spike_key, 0, NO_PAYLOAD)) {
        spin1_delay_us(1);
    }
}

//! \brief records spikes as needed
//! \param[in] neuron_id: the neurons to store spikes from
//! \param[in] n_spikes: the number of times this neuron has spiked
//!
static inline void _mark_spike(uint32_t neuron_id, uint32_t n_spikes) {
    if (recording_flags > 0) {
        if (n_spike_buffers_allocated < n_spikes) {
            uint32_t new_size = 8 + (n_spikes * spike_buffer_size);
            timed_out_spikes *new_spikes = (timed_out_spikes *) spin1_malloc(
                new_size);
            if (new_spikes == NULL) {
                log_error("Cannot reallocate spike buffer");
                rt_error(RTE_SWERR);
            }
            uint32_t *data = (uint32_t *) new_spikes;
            for (uint32_t n = new_size >> 2; n > 0; n--) {
                data[n - 1] = 0;
            }
            if (spikes != NULL) {
                uint32_t old_size =
                    8 + (n_spike_buffers_allocated * spike_buffer_size);
                spin1_memcpy(new_spikes, spikes, old_size);
                sark_free(spikes);
            }
            spikes = new_spikes;
            n_spike_buffers_allocated = n_spikes;
        }
        if (spikes->n_buffers < n_spikes) {
            spikes->n_buffers = n_spikes;
        }
        for (uint32_t n = n_spikes; n > 0; n--) {
            bit_field_set(_out_spikes(n - 1), neuron_id);
        }
    }
}

//! \brief callback for completed recording
void recording_complete_callback() {
    recording_in_progress = false;
}

//! \brief writing spikes to SDRAM
//! \param[in] time: the time to which these spikes are being recorded
static inline void _record_spikes(uint32_t time) {
    while (recording_in_progress) {
        spin1_wfi();
    }
    if ((spikes != NULL) && (spikes->n_buffers > 0)) {
        recording_in_progress = true;
        spikes->time = time;
        recording_record_and_notify(
            0, spikes, 8 + (spikes->n_buffers * spike_buffer_size),
            recording_complete_callback);
        _reset_spikes();
    }
}

//! \brief Timer interrupt callback
//! \param[in] timer_count the number of times this call back has been
//!            executed since start of simulation
//! \param[in] unused for consistency sake of the API always returning two
//!            parameters, this parameter has no semantics currently and thus
//!            is set to 0
//! \return None
void timer_callback(uint timer_count, uint unused) {
    use(unused);

    profiler_write_entry_disable_irq_fiq(PROFILER_ENTER | PROFILER_TIMER);

    time++;

    log_debug("Timer tick %u", time);

    // If a fixed number of simulation ticks are specified and these have passed
    if (infinite_run != TRUE && time >= simulation_ticks) {

        // go into pause and resume state to avoid another tick
        simulation_handle_pause_resume(resume_callback);

        // rewrite poisson params to SDRAM for reading out if needed
        if (!store_poisson_parameters()){
            log_error("Failed to write poisson parameters to SDRAM");
            rt_error(RTE_SWERR);
        }

        profiler_write_entry_disable_irq_fiq(PROFILER_EXIT | PROFILER_TIMER);

        // Finalise any recordings that are in progress, writing back the final
        // amounts of samples recorded to SDRAM
        if (recording_flags > 0) {
            recording_finalise();
        }

        profiler_finalise();

        // Subtract 1 from the time so this tick gets done again on the next
        // run
        time -= 1;
        simulation_ready_to_read();
        return;
    }

    // Set the next expected time to wait for between spike sending
    expected_time = sv->cpu_clk * timer_period;

    // Loop through spike sources
    for (index_t s = 0; s < global_parameters.n_spike_sources; s++) {

        // If this spike source is active this tick
        spike_source_t *spike_source = &poisson_parameters[s];

        // Choose between fast or slow spike sources
        if (spike_source->is_fast_source) {
            if (time >= spike_source->start_ticks
                    && time < spike_source->end_ticks) {

                // Get number of spikes to send this tick
                uint32_t num_spikes = 0;
                // If sqrt_lambda has been set then use the Gaussian algorithm for faster sources
                if (REAL_COMPARE(spike_source->sqrt_lambda, >, REAL_CONST(0.0))) {
                    profiler_write_entry_disable_irq_fiq(PROFILER_ENTER | PROFILER_PROB_FUNC);
                    num_spikes = faster_spike_source_get_num_spikes(
                            spike_source->sqrt_lambda);
                    profiler_write_entry_disable_irq_fiq(PROFILER_EXIT | PROFILER_PROB_FUNC);
                } else {
                    // Call the fast source Poisson algorithm
                    profiler_write_entry_disable_irq_fiq(PROFILER_ENTER | PROFILER_PROB_FUNC);
                    num_spikes = fast_spike_source_get_num_spikes(
                            spike_source->exp_minus_lambda);
                    profiler_write_entry_disable_irq_fiq(PROFILER_EXIT | PROFILER_PROB_FUNC);
                }

                log_debug("Generating %d spikes", num_spikes);

                // If there are any
                if (num_spikes > 0) {

                    // Write spikes to out spikes
                    _mark_spike(s, num_spikes);

                    // If no key has been given, do not send spikes to fabric
                    if (global_parameters.has_key) {

                        // Send spikes
                        const uint32_t spike_key = global_parameters.key | s;
                        for (uint32_t index = 0; index < num_spikes; index++) {
                            _send_spike(spike_key, timer_count);
                        }
                    }
                }
            }
        } else {
            // Handle slow sources
            if ((time >= spike_source->start_ticks)
                    && (time < spike_source->end_ticks)
                    && (spike_source->mean_isi_ticks != 0)) {

                // Mark a spike while the "timer" is below the scale factor value
                while (spike_source->time_to_spike_ticks < ISI_SCALE_FACTOR) {

                    // Write spike to out_spikes
                    _mark_spike(s, 1);

                    // if no key has been given, do not send spike to fabric.
                    if (global_parameters.has_key) {

                        // Send package
                        _send_spike(global_parameters.key | s, timer_count);
                    }

                    // Update time to spike (note, this might not get us back above
                    // the scale factor, particularly if the mean_isi is smaller)
                    profiler_write_entry_disable_irq_fiq(PROFILER_ENTER | PROFILER_PROB_FUNC);
                    spike_source->time_to_spike_ticks +=
                        slow_spike_source_get_time_to_spike(
                            spike_source->mean_isi_ticks);
                    profiler_write_entry_disable_irq_fiq(PROFILER_EXIT | PROFILER_PROB_FUNC);
                }

                // Now we have finished for this tick, subtract the scale factor
                spike_source->time_to_spike_ticks -= ISI_SCALE_FACTOR;
            }
        }
    }

    profiler_write_entry_disable_irq_fiq(PROFILER_EXIT | PROFILER_TIMER);

    // Record output spikes if required
    if (recording_flags > 0) {
        _record_spikes(time);
    }

    if (recording_flags > 0) {
        recording_do_timestep_update(time);
    }

}

//! \brief set the spike source rate as required
//! \param[in] id, the ID of the source to be updated
//! \param[in] rate, the REAL-valued rate in Hz, to be multiplied
//!            to get per_tick values
void set_spike_source_rate(uint32_t id, REAL rate) {
    if ((id >= global_parameters.first_source_id) &&
            ((id - global_parameters.first_source_id) <
             global_parameters.n_spike_sources)) {
        uint32_t sub_id = id - global_parameters.first_source_id;
        REAL rate_per_tick = rate * global_parameters.seconds_per_tick;
        log_debug("Setting rate of %u (%u) to %kHz (%k per tick)",
                id, sub_id, rate, rate_per_tick);
        if (rate_per_tick >= global_parameters.slow_rate_per_tick_cutoff) {
            poisson_parameters[sub_id].is_fast_source = true;
            if (rate_per_tick >= global_parameters.fast_rate_per_tick_cutoff) {
                poisson_parameters[sub_id].sqrt_lambda =
                        SQRT(rate_per_tick); // warning: sqrtk is untested...
            } else {
                poisson_parameters[sub_id].exp_minus_lambda =
                        (UFRACT) EXP(-rate_per_tick);
                poisson_parameters[sub_id].sqrt_lambda = REAL_CONST(0.0);
            }
        } else {
            poisson_parameters[sub_id].is_fast_source = false;
            poisson_parameters[sub_id].mean_isi_ticks =
                    (uint32_t) (REAL_CONST(1.0) / rate_per_tick);
            poisson_parameters[sub_id].time_to_spike_ticks =
                    slow_spike_source_get_time_to_spike(
                        poisson_parameters[sub_id].mean_isi_ticks);
        }
    }
}

// Is this function actually used any more?
void sdp_packet_callback(uint mailbox, uint port) {
    use(port);
    sdp_msg_t *msg = (sdp_msg_t *) mailbox;
    uint32_t *data = (uint32_t *) &(msg->cmd_rc);

    uint32_t n_items = data[0];
    data = &(data[1]);
    for (uint32_t item = 0; item < n_items; item++) {
        uint32_t id = data[(item * 2)];
        REAL rate = kbits(data[(item * 2) + 1]);
        set_spike_source_rate(id, rate);
    }
    spin1_msg_free(msg);
}

//! multicast callback used to set rate when injected in a live example
void multicast_packet_callback(uint key, uint payload) {
    uint32_t id = key & global_parameters.set_rate_neuron_id_mask;
    REAL rate = kbits(payload);
    set_spike_source_rate(id, rate);
}

//! The entry point for this model
void c_main(void) {

    // Load DTCM data
    if (!initialize()) {
        log_error("Error in initialisation - exiting!");
        rt_error(RTE_SWERR);
    }

    // Start the time at "-1" so that the first tick will be 0
    time = UINT32_MAX;

    // Set timer tick (in microseconds)
    spin1_set_timer_tick_and_phase(
        timer_period, global_parameters.timer_offset);

    // Register callback
    spin1_callback_on(TIMER_TICK, timer_callback, TIMER);
    spin1_callback_on(
        MCPL_PACKET_RECEIVED, multicast_packet_callback, MULTICAST);

    simulation_run();
}
