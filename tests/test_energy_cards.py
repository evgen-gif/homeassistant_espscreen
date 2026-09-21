"""The energy cards of the SDS fork (app 0.2.191, firmware 0.2.177): a gauge, a battery and the power flow of a hybrid
inverter, on numeric sensors.

- A gauge takes its range and its zones as tile options (min, max, warn, alarm), numbers the screen reads with the
  state; a battery and the power flow read companion entities named in the `energy` option, which stays in the app:
  the screen gets one small block of watts positive towards the house, the charge and the minutes left.
- The companions are watched like a vacuum's selects, so a change of the grid power redraws the card.
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'screen_manager/app'))
from core import (DISPLAYS, WIDE_ONLY, apply_tile_event, energy_extras, energy_related, extras, screen_options,  # noqa: E402
                  state_message, tile_options, validate_layout)

RUNTIME = (ROOT / 'components/smart_display/runtime_tiles.h').read_text()
MODEL = (ROOT / 'components/smart_display/runtime_model.h').read_text()


def sensor(state, unit='W'):
    return {'state': str(state), 'attributes': {'unit_of_measurement': unit, 'friendly_name': 'x'}}


DEYE = {
    'sensor.ss_load_power': sensor(424), 'sensor.ss_grid_power': sensor(19.8), 'sensor.ss_battery_power': sensor(429.2),
    'sensor.ss_battery_soc': sensor(87, '%'), 'sensor.ss_pv_power': sensor(0), 'sensor.big_pv': sensor(1.5, 'kW'),
}


class Options(unittest.TestCase):
    def test_the_displays_are_offered_for_sensors_and_the_flow_needs_a_wide_card(self):
        self.assertEqual(DISPLAYS['sensor'], ('standard', 'watch', 'graph', 'gauge', 'battery', 'energy'))
        self.assertIn('energy', WIDE_ONLY)
        layout = validate_layout({'title': 'x', 'tiles': [{'entity': 'sensor.ss_load_power', 'name': '', 'options': {'display': 'energy'}}]})
        self.assertEqual(layout['tiles'][0]['options']['size'], 'wide')

    def test_a_gauge_keeps_its_numbers_and_refuses_words(self):
        layout = validate_layout({'title': 'x', 'tiles': [{'entity': 'sensor.apc_load', 'name': '', 'options': {'display': 'gauge', 'min': '0', 'max': 100, 'warn': '70.5', 'alarm': ''}}]})
        self.assertEqual(layout['tiles'][0]['options'], {'display': 'gauge', 'min': 0, 'max': 100, 'warn': 70.5})
        with self.assertRaises(ValueError):
            validate_layout({'title': 'x', 'tiles': [{'entity': 'sensor.apc_load', 'name': '', 'options': {'display': 'gauge', 'max': 'lots'}}]})

    def test_the_companions_are_entity_ids_of_numeric_domains_and_stay_in_the_app(self):
        options = {'display': 'battery', 'energy': {'power': 'sensor.ss_battery_power', 'capacity': '10.24', 'flip': 'true', 'soc': ''}}
        layout = validate_layout({'title': 'x', 'tiles': [{'entity': 'sensor.ss_battery_soc', 'name': '', 'options': options}]})
        tile = layout['tiles'][0]
        self.assertEqual(tile['options']['energy'], {'power': 'sensor.ss_battery_power', 'capacity': 10.24, 'flip': True})
        self.assertNotIn('energy', screen_options(tile, {}))
        for bad in ({'power': 'light.kitchen'}, {'power': 'sensor.a', 'capacity': -1}, {'other': 'sensor.a'}, 'sensor.a'):
            with self.assertRaises(ValueError, msg=bad):
                validate_layout({'title': 'x', 'tiles': [{'entity': 'sensor.ss_battery_soc', 'name': '', 'options': {'display': 'battery', 'energy': bad}}]})
        # Nothing named: the option goes.
        layout = validate_layout({'title': 'x', 'tiles': [{'entity': 'sensor.ss_battery_soc', 'name': '', 'options': {'display': 'battery', 'energy': {'power': ''}}}]})
        self.assertNotIn('energy', layout['tiles'][0]['options'])

    def test_a_tile_event_names_the_companions_flat_or_as_a_dict(self):
        options = tile_options({'display': 'energy', 'grid': 'sensor.ss_grid_power', 'energy': {'soc': 'sensor.ss_battery_soc'}, 'warn': '80'}, {'tap': 'detail'})
        self.assertEqual(options['energy'], {'soc': 'sensor.ss_battery_soc', 'grid': 'sensor.ss_grid_power'})
        self.assertEqual(options['warn'], 80)
        self.assertEqual(options['size'], 'wide')
        layout = apply_tile_event({'title': 'x', 'tiles': []}, 'add', {'entity': 'sensor.ss_load_power', 'display': 'energy', 'grid': 'sensor.ss_grid_power', 'warn': '80'})
        self.assertEqual(layout['tiles'][0]['options']['energy'], {'grid': 'sensor.ss_grid_power'})
        self.assertEqual(layout['tiles'][0]['options']['warn'], 80)


class Extras(unittest.TestCase):
    def test_the_flow_travels_positive_towards_the_house(self):
        tile = {'entity': 'sensor.ss_load_power', 'name': '', 'options': {'display': 'energy', 'energy': {
            'grid': 'sensor.ss_grid_power', 'power': 'sensor.ss_battery_power', 'soc': 'sensor.ss_battery_soc', 'solar': 'sensor.big_pv', 'flip': True}}}
        self.assertEqual(energy_extras(tile, DEYE), {'e': {'g': 20, 'b': -429, 'p': 1500, 's': 87}})
        self.assertEqual(energy_related(tile), ('sensor.ss_battery_power', 'sensor.ss_grid_power', 'sensor.big_pv', 'sensor.ss_battery_soc'))
        # The extras reach the state message; a plain sensor tile carries none.
        message = state_message(0, tile, DEYE, extras(tile, DEYE))
        self.assertEqual(message['x'], {'e': {'g': 20, 'b': -429, 'p': 1500, 's': 87}})
        plain = {'entity': 'sensor.ss_load_power', 'name': ''}
        self.assertIsNone(extras(plain, DEYE))
        self.assertEqual(energy_related(plain), ())

    def test_a_battery_takes_its_charge_from_its_own_sensor_and_counts_the_time_left(self):
        tile = {'entity': 'sensor.ss_battery_soc', 'name': '', 'options': {'display': 'battery', 'energy': {'power': 'sensor.ss_battery_power', 'capacity': 10, 'flip': True}}}
        # 87 % of 10 kWh at 429 W: a little over twenty hours.
        self.assertEqual(energy_extras(tile, DEYE), {'e': {'b': -429, 's': 87, 'm': 1216}})
        charging = {**DEYE, 'sensor.ss_battery_power': sensor(-2000)}
        self.assertEqual(energy_extras(tile, charging)['e']['m'], 39)
        idle = {**DEYE, 'sensor.ss_battery_power': sensor(3)}
        self.assertNotIn('m', energy_extras(tile, idle)['e'])
        gone = {**DEYE, 'sensor.ss_battery_power': {'state': 'unavailable', 'attributes': {}}}
        self.assertEqual(energy_extras(tile, gone), {'e': {'s': 87}})


class Firmware(unittest.TestCase):
    def test_the_screen_reads_the_block_and_draws_the_three_cards(self):
        self.assertIn('extra["e"]', RUNTIME)
        for name in ('render_gauge', 'render_battery', 'render_energy'):
            self.assertIn(f'inline void {name}(', RUNTIME)
        self.assertIn('flow_minutes', MODEL)
        # The dots of the flow step once a second while the screen is awake.
        self.assertIn('w.extra_mode=="flow"', RUNTIME)


if __name__ == '__main__':
    unittest.main()
