# DTED reference fixtures

Published test data from the GDAL project, used to check Ridgeline's DTED
decoder against an independent implementation rather than only against its own
synthetic fixtures. GDAL is distributed under the MIT/X11 licence.

| File | Source | SHA-256 |
| --- | --- | --- |
| `n43.dt0` | [`autotest/gdrivers/data/n43.dt0`](https://raw.githubusercontent.com/OSGeo/gdal/master/autotest/gdrivers/data/n43.dt0) | `6FB965D94585ECB391F4B4194EDD7BD9AAD58C725E2CDBB376FD6F7C69688338` |
| `n43_wgs72.dt0` | `data/dted/n43_wgs72.dt0` in the same repository | `74E76027797F5F3035EF08FC20357F1762F5D12A11CDB1C570F2865ACCE75C76` |

Both are 34 KB. They are committed rather than downloaded so the reference
checks are reproducible offline and cannot quietly stop running.

## What they establish

`n43.dt0` is a 121 x 121 DTED Level 0 tile covering 43-44N, 79-80W at
30-arcsecond spacing. `dted_decoding_tests` checks its dimensions, sample
interval and corner registration, then compares decoded elevations against
GDAL's published checksums:

- full grid `49187`, and the 5x5 subwindow at (5, 5) `305`
- expectations from [`autotest/gdrivers/dted.py`](https://github.com/OSGeo/gdal/blob/master/autotest/gdrivers/dted.py)
  (`test_dted_1`, `test_dted_4`)
- checksum algorithm from [`alg/gdalchecksum.cpp`](https://github.com/OSGeo/gdal/blob/master/alg/gdalchecksum.cpp),
  computed in north-first raster order rather than Ridgeline's south-first
  storage order

`n43_wgs72.dt0` declares a WGS72 horizontal datum. The decoder does not
reproject, so it must reject the file rather than label it WGS84.

## What they do not establish

These are small modulo checksums; they can collide and do not prove every
elevation is correct. They say nothing about dataset accuracy, vertical datum,
or RF prediction accuracy. The synthetic exact-sample cases in
`tests/test_dted_decoding.cpp` remain the primary per-sample coverage.
