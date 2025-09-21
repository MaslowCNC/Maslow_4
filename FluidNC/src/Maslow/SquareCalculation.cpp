#include "SquareCalculation.h"
#include <cmath>

namespace SquareCalculation {

/*
* Heron formula for triangle area given triangle sides
*/
double heronTriangleArea(double a, double b, double c) {
    double s = (a + b + c) / 2.0;
    return sqrt(s * (s - a) * (s - b) * (s - c));
}

/*
* Calculate side length of square ABCD given distances a, b, c, d
* from interior point E to vertices using the Stack Exchange algorithm
* 
* The algorithm uses surface area decomposition:
* 1. 4 orthogonal equilateral triangles contribute (a² + b² + c² + d²)/2
* 2. 4 diagonal triangles with sides (a,√2*b,c), (b,√2*c,d), (c,√2*d,a), (d,√2*a,b)
* 3. Total surface = twice the square area, so L = √(surface/2)
*/
double calculateSquareSideLength(double a, double b, double c, double d) {
    // 4 orthogonal equilateral triangles contribution
    double surface = (a * a + b * b + c * c + d * d) / 2.0;

    // 4 extra triangular triangles with one side * sqrt(2)
    surface += heronTriangleArea(a, sqrt(2.0) * b, c);
    surface += heronTriangleArea(b, sqrt(2.0) * c, d);
    surface += heronTriangleArea(c, sqrt(2.0) * d, a);
    surface += heronTriangleArea(d, sqrt(2.0) * a, b);

    // surface is twice square surface, so side length is square root of (surface / 2)
    return sqrt(surface / 2.0);
}

}