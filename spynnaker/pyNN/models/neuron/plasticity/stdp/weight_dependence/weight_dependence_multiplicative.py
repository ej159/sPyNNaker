from data_specification.enums.data_type import DataType
from spynnaker.pyNN.models.neuron.plasticity.stdp.weight_dependence\
    .abstract_weight_dependence import AbstractWeightDependence


class WeightDependenceMultiplicative(AbstractWeightDependence):

    def __init__(self, w_min=0.0, w_max=1.0, A_plus=0.01, A_minus=0.01):
        AbstractWeightDependence.__init__(self)
        self._w_min = w_min
        self._w_max = w_max
        self._A_plus = A_plus
        self._A_minus = A_minus

    @property
    def w_min(self):
        return self._w_min

    @property
    def w_max(self):
        return self._w_max

    @property
    def A_plus(self):
        return self._A_plus

    @property
    def A_minus(self):
        return self._A_minus

    def is_same_as(self, weight_dependence):
        if not isinstance(weight_dependence, WeightDependenceMultiplicative):
            return False
        return (
            (self._w_min == weight_dependence._w_min) and
            (self._w_max == weight_dependence._w_max) and
            (self._A_plus == weight_dependence._A_plus) and
            (self._A_minus == weight_dependence._A_minus))

    @property
    def vertex_executable_suffix(self):
        return "multiplicative"

    def get_paramters_sdram_usage_in_bytes(
            self, n_synapse_types, n_weight_terms):
        if n_weight_terms != 1:
            raise NotImplementedError(
                "Multiplicative weight dependence only supports single terms")

        return (4 * 4) * n_synapse_types

    def write_parameters(
            self, spec, machine_time_step, weight_scales, n_weight_terms):
        if n_weight_terms != 1:
            raise NotImplementedError(
                "Multiplicative weight dependence only supports single terms")

        # Loop through each synapse type's weight scale
        for w in weight_scales:
            spec.write_value(
                data=int(round(self._w_min * w)), data_type=DataType.INT32)
            spec.write_value(
                data=int(round(self._w_max * w)), data_type=DataType.INT32)

            spec.write_value(
                data=int(round(self._A_plus * w)), data_type=DataType.INT32)
            spec.write_value(
                data=int(round(self._A_minus * w)), data_type=DataType.INT32)

    @property
    def weight_maximum(self):
        return self._w_max
