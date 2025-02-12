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

#ifndef _PRE_EVENTS_H_
#define _PRE_EVENTS_H_

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// Include debug header for log_info etc
#include <debug.h>

//---------------------------------------
// Macros
//---------------------------------------
#define MAX_PRE_SYNAPTIC_EVENTS 4

//---------------------------------------
// Structures
//---------------------------------------
typedef struct {
    uint32_t count_minus_one;

    uint32_t times[MAX_PRE_SYNAPTIC_EVENTS];
    pre_trace_t traces[MAX_PRE_SYNAPTIC_EVENTS];
} pre_event_history_t;

typedef struct {
    pre_trace_t prev_trace;
    uint32_t prev_time;
    const pre_trace_t *next_trace;
    const uint32_t *next_time;
    uint32_t num_events;
} pre_event_window_t;

//---------------------------------------
// Inline functions
//---------------------------------------
static inline pre_event_window_t pre_events_get_window(
        uint32_t time, const pre_event_history_t *events, uint32_t delay,
        uint32_t begin_time) {

    // Start at end event - beyond end of post-event history
    const uint32_t count = events->count_minus_one + 1;
    const uint32_t *end_event_time = events->times + count;
    const uint32_t *event_time = end_event_time;

    // **THINK could window.prev_time be used directly
    // rather than delayed_event_time?
    uint32_t delayed_event_time;
    pre_event_window_t window;

    // Keep looping while event occured after start
    // Of window and we haven't hit beginning of array
    do {

        // Cache pointer to this event as potential
        // Next event and go back one event
        // **NOTE** next_time can be invalid
        window.next_time = event_time--;

        // Add delay to event time
        delayed_event_time = *event_time + delay;

        // If this event is still in the future, move the end time back
        if (delayed_event_time >= time) {
            end_event_time = window.next_time;
        }
    } while (delayed_event_time > begin_time && event_time != events->times);

    // Deference event to use as previous
    window.prev_time = delayed_event_time;

    // Calculate number of events
    window.num_events = (end_event_time - window.next_time);

    // Using num_events, find next and previous traces
    const pre_trace_t *end_event_trace = events->traces + count;
    window.next_trace = (end_event_trace - window.num_events);
    window.prev_trace = *(window.next_trace - 1);

    // Return window
    return window;
}

//---------------------------------------
static inline pre_event_window_t pre_events_next(pre_event_window_t window,
                                                 uint32_t delayed_time) {

    // Update previous time
    window.prev_time = delayed_time;
    window.prev_trace = *window.next_trace++;

    // Go onto next event
    window.next_time++;

    // Decrement remining events
    window.num_events--;
    return window;
}

//---------------------------------------
static inline void pre_events_add(uint32_t time, pre_event_history_t *events,
                                  pre_trace_t trace) {
    if (events->count_minus_one < (MAX_PRE_SYNAPTIC_EVENTS - 1)) {
        const uint32_t new_index = ++events->count_minus_one;

        events->times[new_index] = time;
        events->traces[new_index] = trace;
    } else {

        // **NOTE** 1st element is always an entry at time 0
        for (uint32_t i = 2; i < MAX_PRE_SYNAPTIC_EVENTS; i++) {
            events->times[i - 1] = events->times[i];
            events->traces[i - 1] = events->traces[i];
        }

        // Stick new time and trace at end
        events->times[MAX_PRE_SYNAPTIC_EVENTS - 1] = time;
        events->traces[MAX_PRE_SYNAPTIC_EVENTS - 1] = trace;
    }
}

#endif  // _PRE_EVENTS_H_
