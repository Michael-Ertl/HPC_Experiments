#include <iostream>
#include <random>

struct Point {
  double x;
  double y;
};

const int ITERATION_LIMIT = 1e3;

double square(double value) { return value * value; }

double fitness(const Point &point) {
  const Point peak{3.0, -2.0};
  return 100.0 - square(point.x - peak.x) - square(point.y - peak.y);
}

Point mutation(const Point &point, double stepSize, std::mt19937 &rng) {
  std::uniform_real_distribution<double> randomMove(-stepSize, stepSize);

  return Point{
      point.x + randomMove(rng),
      point.y + randomMove(rng),
  };
}

Point run(Point current) {
  std::mt19937 rng(7);

  double stepSize = 1.0;
  double currentFitness = fitness(current);

  for (int i = 0; i < ITERATION_LIMIT; ++i) {
    Point candidate = mutation(current, stepSize, rng);
    double candidateFitness = fitness(candidate);
    if (candidateFitness > currentFitness) {
      current = candidate;
      currentFitness = candidateFitness;
    }
  }
  return current;
}

int main() {
  Point start{-8.0, 6.0};
  Point best = run(start);
  std::cout << "Start:   (" << start.x << ", " << start.y << ")\n";
  std::cout << "Best:    (" << best.x << ", " << best.y << ")\n";
  std::cout << "Height:  " << fitness(best) << '\n';

  return 0;
}
