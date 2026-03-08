#!/usr/bin/env python3
"""
Visualize different initialization solutions for a VRPTW instance.

Usage:
    python3 scripts/visualize_init_solutions.py --instance c101
    python3 scripts/visualize_init_solutions.py --instance r201 --output results.png
"""

import argparse
import csv
import json
import sys
from typing import List, Dict, Any, Optional, Tuple

try:
    import matplotlib.pyplot as plt
    import numpy as np
except ImportError:
    print("Error: matplotlib and numpy required. Install with: pip3 install matplotlib numpy")
    sys.exit(1)


def load_comparison_data(csv_path: str) -> List[Dict[str, Any]]:
    """Load the insertion heuristic comparison data from CSV."""
    data = []
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            data.append({
                'instance': row['Instance'].strip(),
                'method': row['Method'].strip(),
                'cost': float(row['Cost']),
                'routes': int(row['Routes']),
                'feasible': bool(int(row['Feasible']))
            })
    return data


def load_instance_data(instance_name: str) -> Optional[Dict[str, Any]]:
    """Load customer data for a specific instance from optimization_results.db."""
    try:
        import sqlite3
        conn = sqlite3.connect('optimization_results.db')
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
        
        cursor.execute("""
            SELECT customers, routes 
            FROM optimization_stats 
            WHERE instance_name = ? 
            LIMIT 1
        """, (instance_name,))
        
        row = cursor.fetchone()
        conn.close()
        
        if row:
            customers = json.loads(row['customers'])
            routes = json.loads(row['routes'])
            return {'customers': customers, 'routes': routes}
    except Exception as e:
        pass
    return None


def get_customer_coords(customers: List[Dict], customer_id: int) -> Optional[Tuple[float, float]]:
    """Get coordinates for a customer by ID."""
    for customer in customers:
        if customer['id'] == customer_id:
            return (customer['x'], customer['y'])
    return None


def get_depot_coords(customers: List[Dict]) -> Optional[Tuple[float, float]]:
    """Get depot coordinates (customer with id 0)."""
    return get_customer_coords(customers, 0)


def plot_routes_for_methods(instance_data: Dict[str, Any], instance_name: str, methods: List[str], customers: List[Dict], routes_list: List[List[int]]) -> None:
    """Plot routes for each init method."""
    n_methods = len(methods)
    n_cols = min(2, n_methods)
    n_rows = (n_methods + n_cols - 1) // n_cols
    
    fig, axes = plt.subplots(n_rows, n_cols, figsize=(6 * n_cols, 5 * n_rows))
    if n_methods == 1:
        axes = [axes]
    elif n_rows == 1:
        axes = list(axes)
    else:
        axes = axes.flatten()
    
    depot = get_depot_coords(customers)
    colors = plt.cm.tab10(np.linspace(0, 1, 10))
    
    for idx, (method, routes) in enumerate(zip(methods, routes_list)):
        ax = axes[idx]
        
        if not customers:
            ax.text(0.5, 0.5, f"No customer data\nfor {instance_name}", 
                   ha='center', va='center', fontsize=14)
            ax.set_title(f"{method}")
            continue
        
        ax.scatter(depot[0], depot[1], c='red', s=300, marker='s', 
                  edgecolors='black', linewidth=2, zorder=5, label='Depot')
        
        customer_positions = {}
        for customer in customers:
            if customer['id'] != 0:
                customer_positions[customer['id']] = (customer['x'], customer['y'])
                ax.scatter(customer['x'], customer['y'], c='lightblue', s=100,
                          edgecolors='navy', linewidth=1, zorder=4, alpha=0.7)
        
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
            
            ax.plot(path_x, path_y, c=color, linewidth=2, alpha=0.7,
                   label=f'Route {i+1}', zorder=3)
        
        cost = instance_data[method]['cost']
        ax.set_title(f"{method}\nCost: {cost:.1f}, Routes: {len(routes)}", fontweight='bold')
        ax.set_xlabel('X Coordinate')
        ax.set_ylabel('Y Coordinate')
        ax.grid(True, alpha=0.3, linestyle='--')
        ax.legend(loc='upper left', fontsize=8)
        ax.set_aspect('equal')
    
    for i in range(n_methods, len(axes)):
        axes[i].set_visible(False)
    
    fig.suptitle(f"Initialization Methods Comparison: {instance_name}", fontsize=14, fontweight='bold')
    plt.tight_layout()
    plt.show()


def main():
    parser = argparse.ArgumentParser(description='Visualize different init solutions for a VRPTW instance')
    parser.add_argument('--instance', '-i', required=True, help='Instance name (e.g., c101, r201)')
    parser.add_argument('--csv', default='insertion_heuristic_comparison.csv', help='CSV file path')
    parser.add_argument('--output', '-o', help='Save figure to file instead of showing')
    
    args = parser.parse_args()
    
    data = load_comparison_data(args.csv)
    
    instance_data = {row['method']: row for row in data if row['instance'].lower() == args.instance.lower()}
    
    if not instance_data:
        print(f"Error: No data found for instance '{args.instance}'")
        print(f"Available instances: {set(row['instance'] for row in data)}")
        sys.exit(1)
    
    methods = list(instance_data.keys())
    costs = [instance_data[m]['cost'] for m in methods]
    routes = [instance_data[m]['routes'] for m in methods]
    feasible = [instance_data[m]['feasible'] for m in methods]
    
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))
    
    colors = plt.cm.Set2(np.linspace(0, 1, len(methods)))
    
    bars1 = axes[0].bar(methods, costs, color=colors, edgecolor='black', linewidth=1.5)
    axes[0].set_xlabel('Initialization Method', fontsize=12)
    axes[0].set_ylabel('Total Cost (Distance)', fontsize=12)
    axes[0].set_title(f'{args.instance}: Cost by Initialization Method', fontsize=14, fontweight='bold')
    axes[0].tick_params(axis='x', rotation=45)
    axes[0].grid(True, alpha=0.3, axis='y')
    
    for bar, cost in zip(bars1, costs):
        axes[0].text(bar.get_x() + bar.get_width()/2, bar.get_height() + max(costs)*0.01,
                    f'{cost:.0f}', ha='center', va='bottom', fontsize=9)
    
    min_cost = min(costs)
    for i, (bar, cost) in enumerate(zip(bars1, costs)):
        if cost == min_cost:
            bar.set_edgecolor('gold')
            bar.set_linewidth(3)
    
    bars2 = axes[1].bar(methods, routes, color=colors, edgecolor='black', linewidth=1.5)
    axes[1].set_xlabel('Initialization Method', fontsize=12)
    axes[1].set_ylabel('Number of Routes', fontsize=12)
    axes[1].set_title(f'{args.instance}: Routes by Initialization Method', fontsize=14, fontweight='bold')
    axes[1].tick_params(axis='x', rotation=45)
    axes[1].grid(True, alpha=0.3, axis='y')
    axes[1].set_ylim(0, max(routes) + 1)
    
    for bar, r in zip(bars2, routes):
        axes[1].text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1,
                    f'{r}', ha='center', va='bottom', fontsize=10)
    
    min_routes = min(routes)
    for i, (bar, r) in enumerate(zip(bars2, routes)):
        if r == min_routes:
            bar.set_edgecolor('gold')
            bar.set_linewidth(3)
    
    best_method = min(methods, key=lambda m: instance_data[m]['cost'])
    fig.suptitle(f'Initialization Methods Comparison: {args.instance}\nBest: {best_method}', 
                fontsize=14, fontweight='bold')
    
    plt.tight_layout()
    
    if args.output:
        plt.savefig(args.output, dpi=150, bbox_inches='tight')
        print(f"Saved to {args.output}")
    else:
        plt.show()
    
    print(f"\n{'='*60}")
    print(f"Summary for {args.instance}")
    print(f"{'='*60}")
    print(f"{'Method':<25} {'Cost':>12} {'Routes':>10} {'Feasible':>10}")
    print(f"{'-'*60}")
    for m in methods:
        marker = " <-- BEST" if m == best_method else ""
        print(f"{m:<25} {instance_data[m]['cost']:>12.2f} {instance_data[m]['routes']:>10} {str(instance_data[m]['feasible']):>10}{marker}")
    print(f"{'='*60}\n")


if __name__ == '__main__':
    main()
