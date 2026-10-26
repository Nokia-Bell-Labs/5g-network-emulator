# Explicit UE Environment and Penetration Model

## Configuration

The UE environment type and penetration are configured independently:

```ini
[UE]
ue_location_type: outdoor
building_penetration: none
vehicle_penetration: standard
```

### `ue_location_type`

- `outdoor`
- `indoor`
- `vehicle`
- `random`

### `building_penetration`

- `none`
- `low_loss`
- `high_loss`

### `vehicle_penetration`

- `standard`
- `metallized`

The legacy integer `o2i` key is deprecated. It remains readable during
migration and emits a warning.

## Ownership

- Macroscopic maps own basic scenario path loss, LOS/NLOS state, and shadow
  fading.
- Runtime UE state owns facade penetration, indoor depth, and vehicle
  penetration.
- O2I values are drawn once from a UE-shared deterministic environment stream,
  are identical in DL and UL, and remain stable for the session.

## Indoor scenarios

For InH/InF scenarios where both the gNB and UE are indoors:

```ini
ue_location_type: indoor
building_penetration: none
```

No exterior facade loss is applied.

## Building penetration

Low-loss and high-loss profiles use the approved TR 38.901 material-mixture
formulas and add one stable UE-specific variation:

- low loss: standard deviation 4.4 dB;
- high loss: standard deviation 6.5 dB.

Indoor depth is generated only for an indoor UE with an enabled building
penetration profile.

## Vehicle penetration

- `standard`: \(N(9,5^2)\) dB;
- `metallized`: \(N(20,5^2)\) dB.

Loss is clamped at zero and does not include building indoor-depth loss.

## Reproducibility

UE penetration draws derive from the configured global seed and a keyed
UE-shared environment stream. Fast fading, interference, and distance-CQI
draws use separate keyed streams, so enabling penetration does not shift their
sequences. Runs with the same configuration and seed reproduce the same
values.
