#pragma once

/*
* Stack Exchange algorithm for calculating square side length from distances
* to vertices from any interior point
* 
* Reference: https://math.stackexchange.com/questions/2654730/
*/

namespace SquareCalculation {
    /*
    * Heron formula for triangle area given triangle sides
    */
    double heronTriangleArea(double a, double b, double c);

    /*
    * Calculate side length of square ABCD given distances a, b, c, d
    * from interior point E to vertices using the Stack Exchange algorithm
    */
    double calculateSquareSideLength(double a, double b, double c, double d);
}