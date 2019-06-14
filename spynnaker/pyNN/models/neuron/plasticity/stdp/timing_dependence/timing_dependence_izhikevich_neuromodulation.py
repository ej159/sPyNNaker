from data_specification.enums import DataType
import logging
from spinn_utilities.overrides import overrides
from spynnaker.pyNN.models.neuron.plasticity.stdp.common \
    import plasticity_helpers
from .abstract_timing_dependence import AbstractTimingDependence
from spynnaker.pyNN.models.neuron.plasticity.stdp.synapse_structure\
    import SynapseStructureWeightEligibilityTrace

logger = logging.getLogger(__name__)

LOOKUP_TAU_PLUS_SIZE = 256
LOOKUP_TAU_PLUS_SHIFT = 0
LOOKUP_TAU_MINUS_SIZE = 256
LOOKUP_TAU_MINUS_SHIFT = 0
LOOKUP_TAU_C_SIZE = 520
LOOKUP_TAU_C_SHIFT = 4
LOOKUP_TAU_D_SIZE = 370
LOOKUP_TAU_D_SHIFT = 2


class TimingDependenceIzhikevichNeuromodulation(AbstractTimingDependence):
    __slots__ = [
        "_synapse_structure",
        "_tau_minus",
        "_tau_minus_last_entry",
        "_tau_plus",
        "_tau_plus_last_entry",
        "_tau_c",
        "_tau_c_last_entry",
        "_tau_d",
        "_tau_d_last_entry"]

    def __init__(self, tau_plus=20.0, tau_minus=20.0, tau_c=1000, tau_d=200):
        self._tau_plus = tau_plus
        self._tau_minus = tau_minus
        self._tau_c = tau_c
        self._tau_d = tau_d
        self._synapse_structure = SynapseStructureWeightEligibilityTrace()
        self._tau_plus_last_entry = None
        self._tau_minus_last_entry = None
        self._tau_c_last_entry = None
        self._tau_d_last_entry = None

    @property
    def tau_plus(self):
        return self._tau_plus

    @property
    def tau_minus(self):
        return self._tau_minus

    @overrides(AbstractTimingDependence.is_same_as)
    def is_same_as(self, timing_dependence):
        if not isinstance(timing_dependence,
                          TimingDependenceIzhikevichNeuromodulation):
            return False
        return ((self.tau_plus == timing_dependence.tau_plus) and
                (self.tau_minus == timing_dependence.tau_minus))

    @property
    def vertex_executable_suffix(self):
        return "izhikevich_neuromodulation"

    @property
    def pre_trace_n_bytes(self):

        # Pair rule requires no pre-synaptic trace when only the nearest
        # Neighbours are considered and, a single 16-bit R1 trace
        return 2

    @overrides(AbstractTimingDependence.get_parameters_sdram_usage_in_bytes)
    def get_parameters_sdram_usage_in_bytes(self):
        return ((2 * (LOOKUP_TAU_PLUS_SIZE + LOOKUP_TAU_MINUS_SIZE +
                      LOOKUP_TAU_C_SIZE + LOOKUP_TAU_D_SIZE)) + 4)

    @property
    def n_weight_terms(self):
        return 1

    @overrides(AbstractTimingDependence.write_parameters)
    def write_parameters(self, spec, machine_time_step, weight_scales):

        # Check timestep is valid
        if machine_time_step != 1000:
            raise NotImplementedError(
                "STDP LUT generation currently only supports 1ms timesteps")

        # Write lookup tables
        self._tau_plus_last_entry = plasticity_helpers.write_exp_lut(
            spec, self._tau_plus, LOOKUP_TAU_PLUS_SIZE,
            LOOKUP_TAU_PLUS_SHIFT)
        self._tau_minus_last_entry = plasticity_helpers.write_exp_lut(
            spec, self._tau_minus, LOOKUP_TAU_MINUS_SIZE,
            LOOKUP_TAU_MINUS_SHIFT)

        # Write Izhikevich model exp look up tables
        self._tau_c_last_entry = plasticity_helpers.write_exp_lut(
            spec, self._tau_c, LOOKUP_TAU_C_SIZE,
            LOOKUP_TAU_C_SHIFT)
        self._tau_d_last_entry = plasticity_helpers.write_exp_lut(
            spec, self._tau_d, LOOKUP_TAU_D_SIZE,
            LOOKUP_TAU_D_SHIFT)

        # Calculate constant component in Izhikevich's model weight update
        # function and write to SDRAM.
        weight_update_component = \
            1 / (-((1.0/self._tau_c) + (1.0/self._tau_d)))
        weight_update_component = \
            plasticity_helpers.float_to_fixed(weight_update_component,
                                              (1 << 11))
        spec.write_value(data=weight_update_component,
                         data_type=DataType.INT32)

    @property
    def synaptic_structure(self):
        return self._synapse_structure

    @overrides(AbstractTimingDependence.get_provenance_data)
    def get_provenance_data(self, pre_population_label, post_population_label):
        prov_data = list()
        prov_data.append(plasticity_helpers.get_lut_provenance(
            pre_population_label, post_population_label, "IzhikevicRule",
            "tau_plus_last_entry", "tau_plus", self._tau_plus_last_entry))
        prov_data.append(plasticity_helpers.get_lut_provenance(
            pre_population_label, post_population_label, "IzhikevichRule",
            "tau_minus_last_entry", "tau_minus", self._tau_minus_last_entry))
        prov_data.append(plasticity_helpers.get_lut_provenance(
            pre_population_label, post_population_label, "IzhikevichRule",
            "tau_c_last_entry", "tau_c", self._tau_c_last_entry))
        prov_data.append(plasticity_helpers.get_lut_provenance(
            pre_population_label, post_population_label, "IzhikevichRule",
            "tau_d_last_entry", "tau_d", self._tau_d_last_entry))
        return prov_data

    @overrides(AbstractTimingDependence.get_parameter_names)
    def get_parameter_names(self):
        return ['tau_plus', 'tau_minus', 'tau_c', 'tau_d']
