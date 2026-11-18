"""Energy flow: solar, house and grid on a tile, and a card with the day so far.

The sensors are the tile's options of kind entity: the app sends their states with the tile's data, so the component
needs nothing from plugin.yaml. register_plugin() takes the id, version, tile memory and the screen's texts from the
manifest and translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

energy_flow_ns = cg.esphome_ns.namespace("energy_flow")
EnergyFlow = energy_flow_ns.class_("EnergyFlow", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(EnergyFlow)}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
