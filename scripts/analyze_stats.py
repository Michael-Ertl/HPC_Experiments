#!/usr/bin/env python3
"""
Script to visualize VRPTW optimization results with 2D route plots.

Usage:
    python3 analyze_stats.py --mode routes              # Show best solutions
    python3 analyze_stats.py --mode compare            # Compare multiple runs
    python3 analyze_stats.py --mode evolution          # Show evolution over time
    python3 analyze_stats.py --mode scatter            # Show time vs improvement
    python3 analyze_stats.py --instance c101 --mode compare  # Compare c101 runs
"""

import sqlite3
import argparse
import json
import sys
from datetime import datetime
from typing import List, Dict, Any, Optional, Tuple

try:
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates
    import numpy as np
    from matplotlib.patches import FancyBboxPatch

    MATPLOTLIB_AVAILABLE = True
except ImportError:
    MATPLOTLIB_AVAILABLE = False
    print(
        "Error: matplotlib and numpy required. Install with: pip3 install matplotlib numpy"
    )
    sys.exit(1)


def parse_routes(routes_str: str) -> List[List[int]]:
    """Parse routes from JSON string."""
    try:
        return json.loads(routes_str)
    except json.JSONDecodeError as e:
        print(f"Warning: Failed to parse routes: {e}")
        return []


def parse_customers(customers_str: str) -> List[Dict[str, Any]]:
    """Parse customer coordinates from JSON string."""
    try:
        return json.loads(customers_str)
    except json.JSONDecodeError as e:
        print(f"Warning: Failed to parse customers: {e}")
        return []


def parse_benchmark_solution(filepath: str) -> Optional[Dict[str, Any]]:
    """
    Parse a benchmark solution file (e.g., c101.txt).

    Expected format:
        Instance name : c101
        Authors       : ...
        ...
        Solution
        Route  1 : 81 78 76 71 70 73 77 79 80
        Route  2 : 57 55 54 53 56 58 60 59
        ...

    Returns:
        Dictionary with instance_name and routes, or None if parsing fails.
    """
    try:
        with open(filepath, "r") as f:
            lines = f.readlines()

        instance_name = None
        routes = []
        in_solution = False

        for line in lines:
            line = line.strip()
            if not line:
                continue

            # Parse instance name
            if line.startswith("Instance name"):
                parts = line.split(":")
                if len(parts) >= 2:
                    instance_name = parts[1].strip()

            # Start of solution section
            elif line == "Solution":
                in_solution = True
                continue

            # Parse routes
            elif in_solution and line.startswith("Route"):
                # Format: "Route  1 : 81 78 76 ..."
                parts = line.split(":")
                if len(parts) >= 2:
                    customers_str = parts[1].strip()
                    if customers_str:
                        route = [int(x) for x in customers_str.split()]
                        routes.append(route)

        if instance_name and routes:
            return {
                "instance_name": instance_name,
                "routes": routes,
                "is_perfect": True,
            }
        return None
    except Exception as e:
        print(f"Warning: Failed to parse {filepath}: {e}")
        return None


def load_benchmark_solutions(
    solutions_dir: str = "benchmark-solutions",
) -> Dict[str, Dict[str, Any]]:
    """
    Load all benchmark solutions from the solutions directory.

    Returns:
        Dictionary mapping instance_name -> solution data
    """
    import os

    solutions = {}

    if not os.path.exists(solutions_dir):
        return solutions

    for filename in os.listdir(solutions_dir):
        if filename.endswith(".txt"):
            filepath = os.path.join(solutions_dir, filename)
            solution = parse_benchmark_solution(filepath)
            if solution:
                solutions[solution["instance_name"]] = solution
                print(
                    f"Loaded perfect solution for {solution['instance_name']}: {len(solution['routes'])} routes"
                )

    return solutions


def enrich_with_perfect_solution(
    stats: Dict[str, Any], perfect_solutions: Dict[str, Dict[str, Any]]
) -> Dict[str, Any]:
    """
    Enrich stats with perfect solution data if available.

    Adds:
        - perfect_routes: the optimal routes
        - perfect_vehicles: number of vehicles in optimal solution
        - gap_from_optimal: percentage gap from optimal (if perfect cost known)
    """
    instance_name = stats.get("instance_name")
    if instance_name in perfect_solutions:
        perfect = perfect_solutions[instance_name]
        stats["perfect_routes"] = perfect["routes"]
        stats["perfect_vehicles"] = len(perfect["routes"])
        stats["has_perfect_solution"] = True
    else:
        stats["has_perfect_solution"] = False

    return stats


def fetch_all_stats(db_path: str) -> List[Dict[str, Any]]:
    """Fetch all optimization stats from the database."""
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()

    cursor.execute("""
        SELECT * FROM optimization_stats 
        ORDER BY timestamp DESC
    """)

    rows = cursor.fetchall()
    conn.close()

    results = []
    for row in rows:
        stats = dict(row)
        stats["routes"] = parse_routes(stats["routes"])
        stats["customers"] = parse_customers(stats["customers"])
        results.append(stats)

    return results


def get_best_solution_per_instance(
    stats_list: List[Dict[str, Any]],
) -> List[Dict[str, Any]]:
    """Get the best solution (lowest final_score) for each unique instance."""
    instances = {}
    for stats in stats_list:
        name = stats["instance_name"]
        if (
            name not in instances
            or stats["final_score"] < instances[name]["final_score"]
        ):
            instances[name] = stats
    return list(instances.values())


def get_customer_coords(
    customers: List[Dict], customer_id: int
) -> Optional[Tuple[float, float]]:
    """Get coordinates for a customer by ID."""
    for customer in customers:
        if customer["id"] == customer_id:
            return (customer["x"], customer["y"])
    return None


def get_depot_coords(customers: List[Dict]) -> Optional[Tuple[float, float]]:
    """Get depot coordinates (customer with id 0)."""
    return get_customer_coords(customers, 0)


def plot_routes_2d(
    stats: Dict[str, Any],
    ax=None,
    show_title: bool = True,
    run_number: int = None,
    highlight_best: bool = False,
) -> None:
    """
    Plot a 2D visualization of routes with customers and depot.

    Args:
        stats: Optimization stats dictionary
        ax: Matplotlib axis (creates new figure if None)
        show_title: Whether to show title
        run_number: Optional run number to display
        highlight_best: Whether to highlight as best solution
    """
    customers = stats["customers"]
    routes = stats["routes"]

    if not customers:
        print(f"No customer data for {stats['instance_name']}")
        return

    # Get depot coordinates
    depot = get_depot_coords(customers)
    if not depot:
        print(f"No depot found for {stats['instance_name']}")
        return

    # Create new figure if no axis provided
    if ax is None:
        fig, ax = plt.subplots(figsize=(12, 10))

    # Color cycle for routes
    colors = plt.cm.tab10(np.linspace(0, 1, 10))

    # Plot depot
    depot_color = "gold" if highlight_best else "red"
    depot_size = 400 if highlight_best else 300
    ax.scatter(
        depot[0],
        depot[1],
        c=depot_color,
        s=depot_size,
        marker="s",
        edgecolors="black",
        linewidth=2,
        zorder=5,
        label="Depot",
    )

    # Extract customer coordinates (excluding depot)
    customer_positions = {}
    for customer in customers:
        if customer["id"] != 0:
            customer_positions[customer["id"]] = (customer["x"], customer["y"])
            ax.scatter(
                customer["x"],
                customer["y"],
                c="lightblue",
                s=100,
                edgecolors="navy",
                linewidth=1,
                zorder=4,
                alpha=0.7,
            )

    # Plot routes as lines
    for i, route in enumerate(routes):
        color = colors[i % len(colors)]

        if not route:
            continue

        # Build path: depot -> customer1 -> customer2 -> ... -> depot
        path_x = [depot[0]]
        path_y = [depot[1]]

        for customer_id in route:
            coords = get_customer_coords(customers, customer_id)
            if coords:
                path_x.append(coords[0])
                path_y.append(coords[1])

        # Return to depot
        path_x.append(depot[0])
        path_y.append(depot[1])

        # Plot route line
        ax.plot(
            path_x,
            path_y,
            c=color,
            linewidth=2,
            alpha=0.7,
            label=f"Vehicle {i + 1}",
            zorder=3,
        )

        # Add direction arrows
        for j in range(len(path_x) - 1):
            mid_x = (path_x[j] + path_x[j + 1]) / 2
            mid_y = (path_y[j] + path_y[j + 1]) / 2
            ax.annotate(
                "",
                xy=(path_x[j + 1], path_y[j + 1]),
                xytext=(mid_x, mid_y),
                arrowprops=dict(arrowstyle="->", color=color, lw=1.5, alpha=0.5),
            )

    # Add customer ID labels
    for customer_id, (x, y) in customer_positions.items():
        ax.annotate(
            str(customer_id),
            (x, y),
            xytext=(3, 3),
            textcoords="offset points",
            fontsize=7,
            alpha=0.8,
        )

    if show_title:
        title = f"{stats['instance_name']}"
        if run_number is not None:
            title += f" (Run #{run_number})"
        if highlight_best:
            title += " [BEST]"
        title += f"\nScore: {stats['final_score']:.2f} (↓{stats['relative_improvement'] * 100:.1f}%) | "
        title += f"Vehicles: {stats['final_vehicles']} | Time: {stats['elapsed_seconds']:.1f}s"

        ax.set_title(title, fontsize=12, fontweight="bold", pad=20)

    ax.set_xlabel("X Coordinate")
    ax.set_ylabel("Y Coordinate")
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.legend(loc="upper left", bbox_to_anchor=(1.02, 1), fontsize=9)
    ax.set_aspect("equal")

    # Add some padding
    x_coords = [c["x"] for c in customers]
    y_coords = [c["y"] for c in customers]
    if x_coords and y_coords:
        x_range = max(x_coords) - min(x_coords)
        y_range = max(y_coords) - min(y_coords)
        ax.set_xlim(min(x_coords) - 0.1 * x_range, max(x_coords) + 0.2 * x_range)
        ax.set_ylim(min(y_coords) - 0.1 * y_range, max(y_coords) + 0.1 * y_range)


def plot_all_routes(stats_list: List[Dict[str, Any]], max_per_fig: int = 4) -> None:
    """Plot the best routes for each unique instance."""
    # Get best solution per instance
    best_solutions = get_best_solution_per_instance(stats_list)

    if not best_solutions:
        print("No data to plot.")
        return

    n_instances = len(best_solutions)

    if n_instances == 1:
        fig, ax = plt.subplots(figsize=(12, 10))
        plot_routes_2d(best_solutions[0], ax, highlight_best=True)
    else:
        # Calculate grid size
        n_cols = min(2, n_instances)
        n_rows = (n_instances + n_cols - 1) // n_cols

        fig, axes = plt.subplots(n_rows, n_cols, figsize=(8 * n_cols, 7 * n_rows))

        if n_instances == 1:
            axes = [axes]
        elif n_rows == 1:
            axes = [axes] if n_cols == 1 else list(axes)
        else:
            axes = axes.flatten()

        for i, stats in enumerate(best_solutions):
            if i < len(axes):
                plot_routes_2d(stats, axes[i], highlight_best=True)

        # Hide unused subplots
        for i in range(n_instances, len(axes)):
            axes[i].set_visible(False)

        fig.suptitle(
            f"Best Solutions (showing {n_instances} unique instances)\n"
            f"Total runs in database: {len(stats_list)}",
            fontsize=14,
            fontweight="bold",
            y=0.995,
        )

    plt.tight_layout()
    plt.show()


def plot_perfect_solution(
    customers: List[Dict[str, Any]],
    routes: List[List[int]],
    ax=None,
    instance_name: str = "",
) -> None:
    """
    Plot a perfect/optimal solution in 2D.

    Similar to plot_routes_2d but styled to indicate it's the benchmark solution.
    """
    if not customers:
        print(f"No customer data for perfect solution {instance_name}")
        return

    depot = get_depot_coords(customers)
    if not depot:
        print(f"No depot found for perfect solution {instance_name}")
        return

    if ax is None:
        fig, ax = plt.subplots(figsize=(12, 10))

    # Use a distinct colormap for perfect solutions (greens)
    colors = plt.cm.Greens(np.linspace(0.4, 0.9, 10))

    # Plot depot as gold star
    ax.scatter(
        depot[0],
        depot[1],
        c="gold",
        s=500,
        marker="*",
        edgecolors="black",
        linewidth=3,
        zorder=5,
        label="Depot (Perfect)",
    )

    # Extract customer positions
    for customer in customers:
        if customer["id"] != 0:
            ax.scatter(
                customer["x"],
                customer["y"],
                c="lightgreen",
                s=100,
                edgecolors="darkgreen",
                linewidth=1,
                zorder=4,
                alpha=0.7,
            )
            ax.annotate(
                str(customer["id"]),
                (customer["x"], customer["y"]),
                xytext=(3, 3),
                textcoords="offset points",
                fontsize=7,
                alpha=0.8,
            )

    # Plot routes with thicker lines
    for i, route in enumerate(routes):
        color = colors[i % len(colors)]

        if not route:
            continue

        path_x = [depot[0]]
        path_y = [depot[1]]

        for customer_id in route:
            coords = get_customer_coords(customers, customer_id)
            if coords:
                path_x.append(coords[0])
                path_y.append(coords[1])

        path_x.append(depot[0])
        path_y.append(depot[1])

        ax.plot(
            path_x,
            path_y,
            c=color,
            linewidth=3,
            alpha=0.8,
            label=f"Route {i + 1}",
            zorder=3,
        )

        # Add arrows
        for j in range(len(path_x) - 1):
            mid_x = (path_x[j] + path_x[j + 1]) / 2
            mid_y = (path_y[j] + path_y[j + 1]) / 2
            ax.annotate(
                "",
                xy=(path_x[j + 1], path_y[j + 1]),
                xytext=(mid_x, mid_y),
                arrowprops=dict(arrowstyle="->", color=color, lw=2, alpha=0.6),
            )

    ax.set_title(
        f"{instance_name} - PERFECT SOLUTION\nBenchmark / Optimal Solution",
        fontsize=12,
        fontweight="bold",
        pad=20,
        color="darkgreen",
    )
    ax.set_xlabel("X Coordinate")
    ax.set_ylabel("Y Coordinate")
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.legend(loc="upper left", bbox_to_anchor=(1.02, 1), fontsize=9)
    ax.set_aspect("equal")

    # Add padding
    x_coords = [c["x"] for c in customers]
    y_coords = [c["y"] for c in customers]
    if x_coords and y_coords:
        x_range = max(x_coords) - min(x_coords)
        y_range = max(y_coords) - min(y_coords)
        ax.set_xlim(min(x_coords) - 0.1 * x_range, max(x_coords) + 0.2 * x_range)
        ax.set_ylim(min(y_coords) - 0.1 * y_range, max(y_coords) + 0.1 * y_range)


def plot_comparison(
    stats_list: List[Dict[str, Any]],
    instance_name: str,
    perfect_solutions: Dict[str, Dict[str, Any]] = {},
) -> None:
    """Compare multiple runs for a specific instance side by side."""
    # Filter for specific instance (exact match)
    instance_runs = [s for s in stats_list if s["instance_name"] == instance_name]

    # Check if we have a perfect solution for this instance
    has_perfect = perfect_solutions and instance_name in perfect_solutions
    perfect_data = perfect_solutions.get(instance_name) if has_perfect else None

    if not instance_runs and not has_perfect:
        print(f"No runs or perfect solution found for instance '{instance_name}'")
        return

    if len(instance_runs) == 1 and not has_perfect:
        print(f"Only one run found for {instance_name}, showing single plot")
        fig, ax = plt.subplots(figsize=(12, 10))
        plot_routes_2d(instance_runs[0], ax, highlight_best=True)
        plt.show()
        return

    # Sort by final score (best first)
    instance_runs.sort(key=lambda x: x["final_score"])

    # Calculate grid: perfect solution + all runs
    n_runs = len(instance_runs)
    n_total = n_runs + (1 if has_perfect else 0)
    n_cols = min(3, n_total)
    n_rows = (n_total + n_cols - 1) // n_cols

    fig, axes = plt.subplots(n_rows, n_cols, figsize=(6 * n_cols, 5 * n_rows))

    if n_total == 1:
        axes = [axes]
    elif n_rows == 1:
        axes = [axes] if n_cols == 1 else list(axes)
    else:
        axes = axes.flatten()

    ax_idx = 0

    # Plot perfect solution first (if available)
    if has_perfect and ax_idx < len(axes):
        # Get customer data from first run (all runs have same customers)
        customers = instance_runs[0]["customers"] if instance_runs else []
        plot_perfect_solution(
            customers, perfect_data["routes"], axes[ax_idx], instance_name
        )
        ax_idx += 1

    # Plot solver runs
    for i, stats in enumerate(instance_runs):
        if ax_idx < len(axes):
            is_best = i == 0  # First one is best after sorting
            plot_routes_2d(
                stats, axes[ax_idx], run_number=i + 1, highlight_best=is_best
            )
            ax_idx += 1

    # Hide unused subplots
    for i in range(ax_idx, len(axes)):
        axes[i].set_visible(False)

    # Build title
    title = f"{instance_name} - Comparison of {n_runs} run(s)"
    if has_perfect:
        title += "\n(First plot: Perfect Solution, Gold depot = best solver run)"
    else:
        title += "\n(Gold depot = best solution, no perfect solution available)"

    fig.suptitle(title, fontsize=14, fontweight="bold")

    plt.tight_layout()
    plt.show()

    # Print comparison table
    print(f"\n{'=' * 100}")
    print(f"Comparison for {instance_name}")
    if has_perfect:
        print(f"Perfect solution available: {len(perfect_data['routes'])} routes")
    else:
        print("No perfect solution available in benchmark-solutions/")
    print(f"{'=' * 100}")
    print(
        f"{'Run':<8} {'Score':>12} {'Improvement':>12} {'Vehicles':>10} {'Time (s)':>10} {'vs Perfect':>12}"
    )
    print("-" * 100)
    for i, stats in enumerate(instance_runs):
        marker = " <-- BEST" if i == 0 else ""
        vs_perfect = ""
        if has_perfect:
            vs_perfect = (
                f"{stats['final_vehicles'] - len(perfect_data['routes']):+d} veh"
            )
        print(
            f"{i + 1:<6}{marker:<2} {stats['final_score']:>12.2f} "
            f"{stats['relative_improvement'] * 100:>11.1f}% "
            f"{stats['final_vehicles']:>10} "
            f"{stats['elapsed_seconds']:>10.1f}s "
            f"{vs_perfect:>12}"
        )
    if has_perfect:
        print("-" * 100)
        print(
            f"{'PERFECT':<8} {'N/A':>12} {'N/A':>12} {len(perfect_data['routes']):>10} {'N/A':>10} {'= optimal':>12}"
        )
    print(f"{'=' * 100}\n")


def plot_evolution(stats_list: List[Dict[str, Any]], instance_name: str) -> None:
    """Plot the evolution of solutions over time."""
    # Filter for specific instance (exact match)
    instance_runs = [s for s in stats_list if s["instance_name"] == instance_name]

    if not instance_runs:
        print(f"No runs found for instance '{instance_name}'")
        return

    if len(instance_runs) < 2:
        print(f"Need at least 2 runs to show evolution, found {len(instance_runs)}")
        return

    # Sort by timestamp
    instance_runs.sort(key=lambda x: x["timestamp"])

    # Create evolution plot
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    # Plot scores over time
    timestamps = [
        datetime.strptime(s["timestamp"], "%Y-%m-%d %H:%M:%S") for s in instance_runs
    ]
    scores = [s["final_score"] for s in instance_runs]
    improvements = [s["relative_improvement"] * 100 for s in instance_runs]

    ax1.plot(range(len(timestamps)), scores, "b-o", linewidth=2, markersize=8)
    ax1.set_xlabel("Run Number")
    ax1.set_ylabel("Final Score")
    ax1.set_title(
        f"{instance_name} - Score Evolution\n(Lower is Better)", fontweight="bold"
    )
    ax1.grid(True, alpha=0.3)

    # Highlight best score
    best_idx = np.argmin(scores)
    ax1.scatter(
        best_idx,
        scores[best_idx],
        c="gold",
        s=200,
        marker="*",
        edgecolors="black",
        linewidth=2,
        zorder=5,
        label="Best",
    )
    ax1.legend()

    # Plot improvement over time
    ax2.plot(range(len(timestamps)), improvements, "g-s", linewidth=2, markersize=8)
    ax2.set_xlabel("Run Number")
    ax2.set_ylabel("Relative Improvement (%)")
    ax2.set_title(
        f"{instance_name} - Improvement Trend\n(Higher is Better)", fontweight="bold"
    )
    ax2.grid(True, alpha=0.3)

    # Highlight best improvement
    best_imp_idx = np.argmax(improvements)
    ax2.scatter(
        best_imp_idx,
        improvements[best_imp_idx],
        c="gold",
        s=200,
        marker="*",
        edgecolors="black",
        linewidth=2,
        zorder=5,
        label="Best",
    )
    ax2.legend()

    fig.suptitle(
        f"Solver Evolution for {instance_name} ({len(instance_runs)} runs)",
        fontsize=14,
        fontweight="bold",
    )
    plt.tight_layout()
    plt.show()

    # Show best solution
    best_solution = instance_runs[best_idx]
    fig2, ax = plt.subplots(figsize=(12, 10))
    plot_routes_2d(best_solution, ax, highlight_best=True)
    plt.show()


def plot_time_vs_improvement_scatter(stats_list: List[Dict[str, Any]]) -> None:
    """Create a scatter plot of elapsed time vs relative improvement."""
    if not stats_list:
        print("No data to plot.")
        return

    # Get best solution per instance for cleaner plot
    best_solutions = get_best_solution_per_instance(stats_list)

    fig, ax = plt.subplots(figsize=(12, 8))

    # Extract data
    times = [s["elapsed_seconds"] for s in best_solutions]
    improvements = [s["relative_improvement"] * 100 for s in best_solutions]
    names = [s["instance_name"] for s in best_solutions]
    final_scores = [s["final_score"] for s in best_solutions]

    # Create scatter plot with color based on final score
    scatter = ax.scatter(
        times,
        improvements,
        s=200,
        c=final_scores,
        cmap="viridis_r",
        alpha=0.7,
        edgecolors="black",
        linewidth=2,
    )

    # Add colorbar
    cbar = plt.colorbar(scatter, ax=ax)
    cbar.set_label("Final Score", fontsize=12)

    # Add instance name labels
    for i, name in enumerate(names):
        ax.annotate(
            name,
            (times[i], improvements[i]),
            xytext=(5, 5),
            textcoords="offset points",
            fontsize=9,
            alpha=0.8,
        )

    # Add trend line
    if len(times) > 1:
        z = np.polyfit(times, improvements, 1)
        p = np.poly1d(z)
        x_trend = np.linspace(min(times), max(times), 100)
        ax.plot(
            x_trend,
            p(x_trend),
            "r--",
            alpha=0.6,
            linewidth=2,
            label=f"Trend: y={z[0]:.3f}x+{z[1]:.2f}",
        )

    ax.set_xlabel("Elapsed Time (seconds)", fontsize=12, fontweight="bold")
    ax.set_ylabel("Relative Improvement (%)", fontsize=12, fontweight="bold")
    ax.set_title(
        f"Time vs Optimization Improvement\n(Showing {len(best_solutions)} best solutions)",
        fontsize=14,
        fontweight="bold",
        pad=20,
    )
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.legend(loc="best", fontsize=10)

    # Add statistics box
    stats_text = (
        f"Instances: {len(best_solutions)}\n"
        f"Avg Time: {np.mean(times):.1f}s\n"
        f"Avg Improvement: {np.mean(improvements):.1f}%\n"
        f"Best: {max(improvements):.1f}%"
    )

    props = dict(boxstyle="round", facecolor="wheat", alpha=0.8)
    ax.text(
        0.02,
        0.98,
        stats_text,
        transform=ax.transAxes,
        fontsize=10,
        verticalalignment="top",
        bbox=props,
    )

    plt.tight_layout()
    plt.show()


def print_summary(stats_list: List[Dict[str, Any]]) -> None:
    """Print a concise summary table showing best results per instance."""
    if not stats_list:
        print("No data found.")
        return

    # Get best solution per instance
    best_solutions = get_best_solution_per_instance(stats_list)

    print("\n" + "=" * 100)
    print(
        f"{'Instance':<20} {'Runs':>6} {'Best Score':>12} {'Best Imp %':>12} {'Vehicles':>10} {'Time (s)':>10}"
    )
    print("=" * 100)

    for stats in best_solutions:
        # Count runs for this instance
        n_runs = sum(
            1 for s in stats_list if s["instance_name"] == stats["instance_name"]
        )

        print(
            f"{stats['instance_name']:<20} {n_runs:>6} "
            f"{stats['final_score']:>12.2f} "
            f"{stats['relative_improvement'] * 100:>11.1f}% "
            f"{stats['final_vehicles']:>10} "
            f"{stats['elapsed_seconds']:>10.1f}"
        )

    print("=" * 100)
    print(f"Unique instances: {len(best_solutions)}")
    print(f"Total runs: {len(stats_list)}")
    print(
        f"Average best improvement: {np.mean([s['relative_improvement'] * 100 for s in best_solutions]):.1f}%"
    )
    print(
        f"Average time: {np.mean([s['elapsed_seconds'] for s in best_solutions]):.1f}s\n"
    )


def main():
    parser = argparse.ArgumentParser(
        description="""
VRPTW Optimization Results Visualizer
=======================================

This script visualizes Vehicle Routing Problem with Time Windows (VRPTW) 
optimization results stored in a SQLite database. It provides multiple 
visualization modes including 2D route maps, performance comparisons, 
and evolution tracking over multiple solver runs.

DATABASE SCHEMA
---------------
The SQLite database ('optimization_results.db') contains optimization statistics
including:
  - Instance metadata (name, timestamp)
  - Performance metrics (initial/final scores, improvements)
  - Solution details (routes, vehicle counts)
  - Customer coordinates for visualization

VISUALIZATION MODES
-------------------
routes    : 2D route maps showing the best solution per instance
            Displays: depot (red square), customers (blue circles), 
            routes (colored lines with arrows), and key metrics

compare   : Side-by-side comparison of all runs for a specific instance
            Shows multiple plots in a grid layout, sorted by score
            Best run highlighted with gold depot marker
            PERFECT SOLUTION: If available in benchmark-solutions/,
            displays it as the first plot for comparison
            
evolution : Track solver improvement over time for a specific instance
            Displays: score trend line, improvement trend line
            Then shows 2D map of the best solution

scatter   : Scatter plot of time vs relative improvement
            X-axis: elapsed seconds, Y-axis: improvement percentage
            Color-coded by final score (better = darker)
            Includes trend line and statistics box

summary   : Text-only summary table showing best results per instance
            Displays: run count, best score, improvement %, vehicles, time

all       : Show all visualizations (summary, routes, scatter)

INSTANCE FILTERING
------------------
Use --instance to filter results by instance name (substring match).
Required for 'compare' and 'evolution' modes.
Examples: c101, r201, rc102

MULTIPLE RUNS HANDLING
----------------------
When running the solver multiple times, the database accumulates results.
The script intelligently handles this:
  - routes mode: Shows best solution per instance (lowest score)
  - compare mode: Shows all runs for specified instance
  - evolution mode: Tracks improvement trends across runs
  - summary mode: Counts runs and shows best per instance

VISUAL ELEMENTS
---------------
- Depot: Red square (or gold star for best solution)
- Customers: Blue circles with ID labels
- Routes: Colored lines with directional arrows
- Legend: Vehicle numbers in top-right corner
- Title: Instance name, score, improvement %, vehicles, time

PREREQUISITES
-------------
Requires: matplotlib, numpy, sqlite3
Install: pip3 install matplotlib numpy

PERFECT SOLUTIONS
-----------------
The script automatically checks benchmark-solutions/ directory for known
optimal solutions (e.g., c101.txt, r201.txt). These are displayed alongside
your solver results for comparison.

Format: Text files with routes in format:
    Instance name : c101
    ...
    Solution
    Route  1 : 81 78 76 71 70 73 77 79 80
    Route  2 : 57 55 54 53 56 58 60 59

To add your own perfect solutions, create .txt files in benchmark-solutions/
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
QUICK START
-----------
1. Run the VRPTW solver:
   ./build/core
   
2. View best solutions:
   python3 scripts/analyze_stats.py
   
3. Compare multiple runs:
   ./build/core                      # Run 2
   ./build/core                      # Run 3
   python3 scripts/analyze_stats.py --mode compare --instance c101

4. Track improvement over time:
   python3 scripts/analyze_stats.py --mode evolution --instance c101

5. Compare with perfect solution:
   python3 scripts/analyze_stats.py --mode compare --instance c101
   # (shows benchmark solution as first plot if available)

EXAMPLES
--------
Show best solution per instance (default):
  %(prog)s
  %(prog)s --mode routes

Compare all runs for c101 (includes perfect solution if available):
  %(prog)s --mode compare --instance c101

Show evolution of c102 over time:
  %(prog)s --mode evolution --instance c102

Show time vs improvement scatter:
  %(prog)s --mode scatter

Show everything:
  %(prog)s --mode all

Filter to specific instance:
  %(prog)s --instance c101              # Best c101 only
  %(prog)s --instance r2                # All r2XX instances

Use custom database:
  %(prog)s --db /path/to/results.db --mode all

TIPS
----
- Press 'q' or close window to advance to next plot
- Use --instance to focus on specific problem instances
- Run solver multiple times to build comparison data
- Evolution mode helps identify if solver is converging
- Scatter plot reveals time-quality tradeoffs

EXIT CODES
----------
0  : Success
1  : Error (database not found, no data, invalid args)

For more information, see the VRPTW solver documentation.
        """,
    )

    parser.add_argument(
        "--mode",
        choices=["routes", "compare", "evolution", "scatter", "summary", "all"],
        default="routes",
        help="""
Visualization mode. 
'routes' (default) shows best solution per instance.
'compare' shows side-by-side comparison for --instance.
'evolution' shows improvement trends for --instance.
'scatter' shows time vs improvement scatter plot.
'summary' prints text summary table only.
'all' shows summary, routes, and scatter.
        """,
    )

    parser.add_argument(
        "--db",
        default="optimization_results.db",
        metavar="PATH",
        help="Path to SQLite database file (default: optimization_results.db)",
    )

    parser.add_argument(
        "--instance",
        metavar="NAME",
        help="""
Filter by instance name (substring match).
Required for 'compare' and 'evolution' modes.
Examples: c101, r201, rc102
        """,
    )

    args = parser.parse_args()

    # Load perfect/benchmark solutions
    perfect_solutions = load_benchmark_solutions()
    if perfect_solutions:
        print(
            f"Loaded {len(perfect_solutions)} perfect solution(s) from benchmark-solutions/"
        )
        for name in perfect_solutions:
            print(f"  - {name}: {len(perfect_solutions[name]['routes'])} routes")

    # Fetch data
    try:
        stats_list = fetch_all_stats(args.db)
    except sqlite3.Error as e:
        print(f"Error accessing database: {e}")
        sys.exit(1)
    except FileNotFoundError:
        print(f"Database not found: {args.db}")
        print("Run the VRPTW solver first to generate the database.")
        sys.exit(1)

    if not stats_list:
        print("No data found in database.")
        sys.exit(1)

    # Enrich stats with perfect solution data
    for stats in stats_list:
        enrich_with_perfect_solution(stats, perfect_solutions)

    # Filter by instance if requested (exact match)
    if args.instance and args.mode not in ["compare", "evolution"]:
        stats_list = [s for s in stats_list if s["instance_name"] == args.instance]
        if not stats_list:
            print(f"No instances matching '{args.instance}' found.")
            sys.exit(1)

    # Display based on mode
    if args.mode in ["summary", "all"]:
        print_summary(stats_list)

    if args.mode in ["routes", "all"]:
        print(f"\nGenerating 2D route visualizations for best solutions...")
        plot_all_routes(stats_list)

    if args.mode == "compare":
        if not args.instance:
            print("Error: --instance required for compare mode")
            sys.exit(1)
        print(f"\nComparing runs for {args.instance}...")
        plot_comparison(stats_list, args.instance, perfect_solutions)

    if args.mode == "evolution":
        if not args.instance:
            print("Error: --instance required for evolution mode")
            sys.exit(1)
        print(f"\nShowing evolution for {args.instance}...")
        plot_evolution(stats_list, args.instance)

    if args.mode in ["scatter", "all"]:
        print(f"\nGenerating time vs improvement scatter plot...")
        plot_time_vs_improvement_scatter(stats_list)


if __name__ == "__main__":
    main()
