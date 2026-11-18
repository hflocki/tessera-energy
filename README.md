# Energy flow

Electricity at home with solar panels on one tile, in the spirit of Home Assistant's energy dashboard.

**The tile** shows the sun, the house and the grid side by side: what the panels make now, what the house uses, and
what comes from the grid or goes back to it. Arrows show where the power flows; a circle takes its colour while power
flows there (amber for the sun, blue for power from the grid, green for power to it). Under them, when there is room:

- a bar of the house's use, amber as far as the sun covers it, blue for the rest ("Solar covers 46 %");
- the day so far ("Today: solar 3.4 kWh · from the grid 5.2 kWh");
- what the panels made against today's forecast, as a bar.

It fits any size from 1 x 1 to 6 x 3: on a narrow tile it shows the grid and, under it, the sun. Each part shows
only while there is room for it and a sensor for it. **A tap** opens a card: at the left the flow as in Home Assistant's energy card, the sun above, the grid and the
house below, joined by lines that take their colour and a dot where power flows; at the right every chosen sensor in
a row of its own (grid, solar, house, coverage, solar today, forecast with how much of it is made, grid today).

## Set up

1. In Tessera, open **Plugins**, **Add with a link**, and paste the link of this repository. Add the plugin to a
   screen and build it once.
2. In **Layout**, place the **Energy** tile and choose its sensors in the tile's settings:
   - **Grid power** (needed): your meter's power now in W or kW, positive while power comes from the grid and
     negative while it goes back. A meter that counts the other way round: switch on **Invert the grid**. A kWh
     counter does not work here; the tile says so.
   - **Solar power**: what the panels make now.
   - **House power**: what the house uses now, if you have such a sensor. Without it the house is grid + solar.
   - **Solar coverage**: a sensor in % if you have one; without it the share is worked out from solar and the house.
   - **Solar today**, **Grid today**: today's energy in kWh, for the day so far.
   - **Forecast today**: today's solar forecast in kWh (Forecast.Solar, Solcast).

   Leave empty what you do not have. Another choice needs no new build: the tile shows it at once.

## Good to know

- Without a **House power** sensor the house's use is grid + solar, so a battery is not part of it. With one (most
  inverters with a battery have such a sensor) the house and the solar coverage count the battery too.
- W and kW, Wh and kWh are both understood: the plugin reads each sensor's unit.
- From 2.0.0 the sensors are chosen per tile, not under **When building**: after the update, choose them again in
  the tile's settings.
