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
 *  \brief utility class which ensures that format of spikes being recorded is
 *   done in a standard way
 *
 *
 *  \details The API includes:
 *     - out_spikes_reset
 *          clears the memory used as a tracker for the next set of spikes
 *          which will be recorded to SDRAM at some point
 *     - out_spikes_initialize
 *          initialises a piece of memory which can contain a flag to say if
 *          any source has spiked between resets
 *     - out_spikes_record
 *          records the current set of flags for each spike source into the
 *          spike recording region in SDRAM (flags to deduce which regions are
 *           active are handed to this method due to recording not containing
 *           them itself). TODO change the recording.h and recording.c to
 *           contain the channels itself.
 *     - out_spikes_is_empty
 *          helper method which checks if the current spikes flags have any
 *          recorded for use.
 *     - out_spikes_is_spike
 *          helper method which checks if a given source has spiked since the
 *           last reset.
 *     - out_spikes_print
 *          a debug function that when the model is compiled in DEBUG mode will
            record into SDRAM the spikes that are currently been recorded as
            having spiked since the last reset command
 *     - out_spikes_set_spike
 *          helper method which allows models to state that a given spike source
            has spiked since the last reset.
 */

#ifndef _OUT_SPIKES_H_
#define _OUT_SPIKES_H_

#include "neuron-typedefs.h"

#include <bit_field.h>
#include <recording.h>

extern bit_field_t out_spikes;

//! \brief clears the currently recorded spikes
void out_spikes_reset();

//! \brief initialise the recording of spikes
//! \param[in] max_spike_sources the number of spike sources to be recorded
//! \return True if the initialisation was successful, false otherwise
bool out_spikes_initialize(size_t max_spike_sources);

//! \brief flush the recorded spikes - must be called to do the actual
//!        recording
//! \param[in] channel The channel to record to
//! \param[in] time The time at which the recording is being made
//! \param[in] n_words The number of words of the buffer to record - allows
//!                    the buffer to be allocated larger than needed
//! \param[in] callback Callback to call when the recording is done
//                      (can be NULL)
bool out_spikes_record(
    uint8_t channel, uint32_t time, uint32_t n_words,
    recording_complete_callback_t callback);

//! \brief Check if any spikes have been recorded
//! \return True if no spikes have been recorded, false otherwise
bool out_spikes_is_empty();

//! \brief Check if a given neuron has been recorded to spike
//! \param[in] spike_source_index The index of the neuron.
//! \return true if the spike source has been recorded to spike
bool out_spikes_is_spike(index_t spike_source_index);

//! \brief print out the contents of the output spikes (in DEBUG only)
void out_spikes_print();

//! \brief Indicates that a neuron has spiked
//! \param[in] spike_source_index The index of the neuron that has spiked
static inline void out_spikes_set_spike(index_t spike_source_index) {
    bit_field_set(out_spikes, spike_source_index);
}

#endif // _OUT_SPIKES_H_
