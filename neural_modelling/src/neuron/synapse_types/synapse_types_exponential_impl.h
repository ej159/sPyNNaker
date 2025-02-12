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
 * \brief implementation of synapse_types.h for Exponential shaping
 *
 * \details This is used to give a simple exponential decay to synapses.
 *
 * If we have combined excitatory/inhibitory synapses it will be
 * because both excitatory and inhibitory synaptic time-constants
 * (and thus propagations) are identical.
 */

#ifndef _SYNAPSE_TYPES_EXPONENTIAL_IMPL_H_
#define _SYNAPSE_TYPES_EXPONENTIAL_IMPL_H_


//---------------------------------------
// Macros
//---------------------------------------
#define SYNAPSE_TYPE_BITS 1
#define SYNAPSE_TYPE_COUNT 2

#define NUM_EXCITATORY_RECEPTORS 1
#define NUM_INHIBITORY_RECEPTORS 1

#include <neuron/decay.h>
#include <debug.h>
#include "synapse_types.h"


//---------------------------------------
// Synapse parameters
//---------------------------------------
input_t excitatory_response[NUM_EXCITATORY_RECEPTORS];
input_t inhibitory_response[NUM_INHIBITORY_RECEPTORS];

typedef struct exp_params_t{
	decay_t decay;
    decay_t init;
    input_t synaptic_input_value;
}exp_params_t;

typedef struct synapse_param_t {
	exp_params_t exc;
	exp_params_t inh;
} synapse_param_t;

typedef enum input_buffer_regions {
    EXCITATORY, INHIBITORY,
} input_buffer_regions;


//---------------------------------------
// Synapse shaping inline implementation
//---------------------------------------

//! \brief decays the stuff thats sitting in the input buffers
//! (to compensate for the valve behaviour of a synapse
//! in biology (spike goes in, synapse opens, then closes slowly) plus the
//! leaky aspect of a neuron). as these have not yet been processed and applied
//! to the neuron.
//! \param[in]  parameter: the pointer to the parameters to use
//! \return nothing
static inline void exp_shaping(exp_params_t* exp_params){

    // decay value according to decay constant
	exp_params->synaptic_input_value =
			decay_s1615(exp_params->synaptic_input_value,
					exp_params->decay);
}

static inline void synapse_types_shape_input(
        synapse_param_pointer_t parameter) {

	exp_shaping(&parameter->exc);
	exp_shaping(&parameter->inh);
}

//! \brief helper function to add input for a given timer period to a given
//! neuron
//! \param[in]  parameter: the pointer to the parameters to use
//! \param[in] input the inputs to add.
//! \return None
static inline void add_input_exp(exp_params_t* exp_params, input_t input){

	exp_params->synaptic_input_value = exp_params->synaptic_input_value +
			decay_s1615(input, exp_params->init);
}

//! \brief adds the inputs for a give timer period to a given neuron that is
//! being simulated by this model
//! \param[in] synapse_type_index the type of input that this input is to be
//! considered (aka excitatory or inhibitory etc)
//! \param[in]  parameter: the pointer to the parameters to use
//! \param[in] input the inputs for that given synapse_type.
//! \return None
static inline void synapse_types_add_neuron_input(
        index_t synapse_type_index, synapse_param_pointer_t parameter,
        input_t input) {

    if (synapse_type_index == EXCITATORY) {
    	add_input_exp(&parameter->exc, input);

    } else if (synapse_type_index == INHIBITORY) {
    	add_input_exp(&parameter->inh, input);
    }
}

//! \brief extracts the excitatory input buffers from the buffers available
//! for a given parameter set
//! \param[in]  parameter: the pointer to the parameters to use
//! \return the excitatory input buffers for a given neuron ID.
static inline input_t* synapse_types_get_excitatory_input(
        synapse_param_pointer_t parameter) {
    excitatory_response[0] = parameter->exc.synaptic_input_value;
    return &excitatory_response[0];
}

//! \brief extracts the inhibitory input buffers from the buffers available
//! for a given parameter set
//! \param[in]  parameter: the pointer to the parameters to use
//! \return the inhibitory input buffers for a given neuron ID.
static inline input_t* synapse_types_get_inhibitory_input(
        synapse_param_pointer_t parameter) {
    inhibitory_response[0] = parameter->inh.synaptic_input_value;
    return &inhibitory_response[0];
}

//! \brief returns a human readable character for the type of synapse.
//! examples would be X = excitatory types, I = inhibitory types etc etc.
//! \param[in] synapse_type_index the synapse type index
//! (there is a specific index interpretation in each synapse type)
//! \return a human readable character representing the synapse type.
static inline const char *synapse_types_get_type_char(
        index_t synapse_type_index) {
    if (synapse_type_index == EXCITATORY) {
        return "X";
    } else if (synapse_type_index == INHIBITORY)  {
        return "I";
    } else {
        log_debug("did not recognise synapse type %i", synapse_type_index);
        return "?";
    }
}

//! \brief prints the input for a neuron ID given the available inputs
//! currently only executed when the models are in debug mode, as the prints
//! are controlled from the synapses.c _print_inputs method.
//! \param[in]  parameter: the pointer to the parameters to use
//! \return Nothing
static inline void synapse_types_print_input(
        synapse_param_pointer_t parameter) {
    io_printf(
        IO_BUF, "%12.6k - %12.6k",
        parameter->exc.synaptic_input_value,
        parameter->inh.synaptic_input_value);
}

//! \brief printer call
//! \param[in] parameter: the pointer to the parameters to print
static inline void synapse_types_print_parameters(
        synapse_param_pointer_t parameter) {
    log_debug("exc_decay = %R\n", (unsigned fract) parameter->exc.decay);
    log_debug("exc_init  = %R\n", (unsigned fract) parameter->exc.init);
    log_debug("inh_decay = %R\n", (unsigned fract) parameter->inh.decay);
    log_debug("inh_init  = %R\n", (unsigned fract) parameter->inh.init);
    log_debug("gsyn_excitatory_initial_value = %11.4k\n",
              parameter->exc.synaptic_input_value);
    log_debug("gsyn_inhibitory_initial_value = %11.4k\n",
              parameter->inh.synaptic_input_value);
}

#endif  // _SYNAPSE_TYPES_EXPONENTIAL_IMPL_H_
