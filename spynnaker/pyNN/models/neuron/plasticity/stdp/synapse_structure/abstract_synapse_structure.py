from six import add_metaclass
from abc import ABCMeta
from abc import abstractmethod


@add_metaclass(ABCMeta)
class AbstractSynapseStructure(object):

    @abstractmethod
    def get_n_bytes_for_connections(self, n_connections):
        """ Get the number of bytes for the given number of connections
        """

    @abstractmethod
    def get_synaptic_data(self, connections, synapse_weight_scale):
        """ Get the plastic synaptic data for this connection
        """
