#include "geometry.h"
#include <stdio.h>

bool prepare_polygon(Polygon *polygon, char *error_message,
                     size_t error_capacity) {
    /* Polygon preparation not implemented for P2. */
    (void)polygon;
    if (error_message && error_capacity > 0) {
        snprintf(error_message, error_capacity,
                 "Polygon preparation is not yet implemented");
    }
    return false;
}

