#!/usr/bin/python

# Copyright (c) 2017-2019 The University of Manchester
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

"""
Synfirechain-like example
"""
import spynnaker8 as p
import spynnaker.plot_utils as plot_utils
from spynnaker.pyNN.utilities import neo_convertor
from spinnaker_testbase import BaseTestCase


def do_run(nNeurons):

    p.setup(timestep=0.1, min_delay=1.0, max_delay=7.5)
    p.set_number_of_neurons_per_core(p.IF_curr_exp, 100)

    cell_params_lif = {'cm': 0.25, 'i_offset': 0.0, 'tau_m': 20.0,
                       'tau_refrac': 2.0, 'tau_syn_E': 6, 'tau_syn_I': 6,
                       'v_reset': -70.0, 'v_rest': -65.0, 'v_thresh': -55.4}

    populations = list()
    projections = list()

    weight_to_spike = 12
    injection_delay = 1
    delay = 1

    spikeArray = {'spike_times': [[0, 10, 20, 30]]}
    populations.append(p.Population(1, p.SpikeSourceArray, spikeArray,
                                    label='pop_0'))
    populations.append(p.Population(nNeurons, p.IF_curr_exp, cell_params_lif,
                                    label='pop_1'))
    populations.append(p.Population(nNeurons, p.IF_curr_exp, cell_params_lif,
                                    label='pop_2'))

    connector = p.AllToAllConnector()
    synapse_type = p.StaticSynapse(weight=weight_to_spike,
                                   delay=injection_delay)
    projections.append(p.Projection(populations[0], populations[1], connector,
                                    synapse_type=synapse_type))
    connector = p.OneToOneConnector()
    synapse_type = p.StaticSynapse(weight=weight_to_spike, delay=delay)
    projections.append(p.Projection(populations[1], populations[2], connector,
                                    synapse_type=synapse_type))

    populations[1].record("v")
    populations[1].record("spikes")

    p.run(100)

    neo = populations[1].get_data(["v", "spikes"])

    v = neo_convertor.convert_data(neo, name="v")
    spikes = neo_convertor.convert_spikes(neo)

    p.end()

    return (v, spikes)


class MwhSynfireWithDifferentMachineTimeSteps(BaseTestCase):

    def test_run(self):
        nNeurons = 3  # number of neurons in each population
        (v, spikes) = do_run(nNeurons)
        self.assertEqual(51, len(spikes))


if __name__ == '__main__':
    nNeurons = 3  # number of neurons in each population
    (v, spikes) = do_run(nNeurons)
    plot_utils.plot_spikes(spikes)
    plot_utils.heat_plot(v, title="v")
