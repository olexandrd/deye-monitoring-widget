# ESP32-C3 SuperMini + OLED case

Ready-to-print enclosure for the stacked module:

- ESP32-C3 SuperMini mounted behind the OLED on pin headers
- 0.96" I2C OLED module with four corner holes
- front display window
- side USB-C cutout
- single STL model

## Files

- `oled-esp.stl` - ready-to-print STL model

## Print Notes

The STL is exported from the latest local case design. Import `oled-esp.stl`
directly into your slicer and verify the following dimensions against your
actual boards before printing:

- OLED module: `33.7 x 35.5 mm`
- OLED screen/window: `23 x 34.5 mm`
- ESP32-C3 SuperMini: `17.9 x 22.9 mm`
- OLED-to-ESP gap: `7.3 mm`
- USB distance from ESP board edge to connector center: `8.4 mm`

If the fit is tight, tune the original CAD model outside this repository and
replace `oled-esp.stl` with a newly exported version.
