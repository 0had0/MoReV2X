#ifndef STATION_TYPE_H
#define STATION_TYPE_H

/*
 * Minimal StationType shim used only by traci-client.{h,cc}.
 * Replaces the full ETSI ASN.1 StationType header from ms-van3t's
 * src/automotive/model/ASN1/. Only the enum values actually referenced
 * by TraciClient are required; the rest are included for completeness
 * and parity with the canonical enum.
 *
 * Must remain C-compatible (no namespaces, no classes).
 */
typedef enum StationType {
    StationType_unknown         = 0,
    StationType_pedestrian      = 1,
    StationType_cyclist         = 2,
    StationType_moped           = 3,
    StationType_motorcycle      = 4,
    StationType_passengerCar    = 5,
    StationType_bus             = 6,
    StationType_lightTruck      = 7,
    StationType_heavyTruck      = 8,
    StationType_trailer         = 9,
    StationType_specialVehicles = 10,
    StationType_tram            = 11,
    StationType_roadSideUnit    = 15
} e_StationType;

typedef long StationType_t;

#endif /* STATION_TYPE_H */
