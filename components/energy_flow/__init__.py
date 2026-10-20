"""Energy flow: solar, house and grid on a tile, and a card with the day so far.

plugin.yaml reads every value from Home Assistant as a text sensor; this component hands them to the C++ by the index
of FIELDS, which energy_flow.h numbers the same way. register_plugin() takes the id, version, tile memory and the
screen's texts from the manifest and translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display, text_sensor
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]
AUTO_LOAD = ["text_sensor"]

# The order is the C++'s (enum Field in energy_flow.h); never reorder one without the other.
FIELDS = ['grid', 'grid_unit', 'pv', 'pv_unit', 'pv_today', 'pv_today_unit', 'coverage', 'forecast', 'forecast_unit',
          'grid_today', 'grid_today_unit']

energy_flow_ns = cg.esphome_ns.namespace("energy_flow")
EnergyFlow = energy_flow_ns.class_("EnergyFlow", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.declare_id(EnergyFlow)}
    | {cv.Optional(name): cv.use_id(text_sensor.TextSensor) for name in FIELDS}
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    for index, name in enumerate(FIELDS):
        if name in config:
            cg.add(var.set_field(index, await cg.get_variable(config[name])))
