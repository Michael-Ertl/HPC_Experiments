import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict
import json
import urllib.request

SOLOMON_BASE_URL = "https://raw.githubusercontent.com/CervEdin/solomon-vrptw-benchmarks/main"

def load_solomon_instance(instance_name: str):
    """Load Solomon benchmark instance data from GitHub."""
    prefix = instance_name[0].lower()
    num = instance_name[1]  # First digit: 1 or 2 for C/R/RC
    if prefix == 'c':
        url = f"{SOLOMON_BASE_URL}/c/{num}/{instance_name.lower()}.json"
    elif prefix == 'r':
        url = f"{SOLOMON_BASE_URL}/r/{num}/{instance_name.lower()}.json"
    else:
        url = f"{SOLOMON_BASE_URL}/rc/{num}/{instance_name.lower()}.json"
    
    try:
        with urllib.request.urlopen(url, timeout=10) as response:
            data = json.loads(response.read().decode())
            return data
    except Exception as e:
        print(f"Warning: Could not load {instance_name}: {e}")
        return None

data = []
with open('insertion_heuristic_comparison.csv', 'r') as f:
    reader = csv.DictReader(f)
    for row in reader:
        if row['Instance'] != 'Instance':
            data.append({
                'Instance': row['Instance'].strip(),
                'Method': row['Method'].strip(),
                'Cost': float(row['Cost']),
                'Routes': int(row['Routes']),
                'Feasible': bool(int(row['Feasible']))
            })

methods = ['Sweep', 'Solomon', 'GreedyMinVehicles', 'NearestNeighbor']
colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']

instance_costs = defaultdict(lambda: {})
instance_routes = defaultdict(lambda: {})
for d in data:
    instance_costs[d['Instance']][d['Method']] = d['Cost']
    instance_costs[d['Instance']][d['Method']] = instance_costs[d['Instance']].get(d['Method'], d['Cost'])
    instance_routes[d['Instance']][d['Method']] = d['Routes']

instances = list(instance_costs.keys())

for inst in instances:
    fig, axes = plt.subplots(3, 2, figsize=(14, 15))
    inst_data = instance_costs[inst]
    
    solomon_data = load_solomon_instance(inst)
    
    ax_geo = axes[0, 0]
    if solomon_data and 'customers' in solomon_data:
        customers = solomon_data['customers']
        depot = [c for c in customers if c['cust-nr'] == 0]
        other_customers = [c for c in customers if c['cust-nr'] != 0]
        
        if depot:
            ax_geo.scatter(depot[0]['x'], depot[0]['y'], c='red', s=300, marker='s', 
                          edgecolors='black', linewidth=2, zorder=5, label='Depot')
        
        if other_customers:
            xs = [c['x'] for c in other_customers]
            ys = [c['y'] for c in other_customers]
            ax_geo.scatter(xs, ys, c='lightblue', s=80, edgecolors='navy', 
                          linewidth=1, zorder=4, alpha=0.7, label='Customers')
        
        ax_geo.set_xlabel('X Coordinate')
        ax_geo.set_ylabel('Y Coordinate')
        ax_geo.set_title(f'Geographical Plot: {inst}')
        ax_geo.legend(loc='upper right')
        ax_geo.grid(True, alpha=0.3, linestyle='--')
        ax_geo.set_aspect('equal')
    else:
        ax_geo.text(0.5, 0.5, f"Instance data\nnot available\nfor {inst}", 
                   ha='center', va='center', fontsize=14)
        ax_geo.set_title(f'Geographical Plot: {inst}')
    
    ax1 = axes[0, 1]
    costs = [inst_data.get(method, 0) for method in methods]
    bars = ax1.bar(methods, costs, color=colors)
    ax1.set_xlabel('Init Method')
    ax1.set_ylabel('Cost')
    ax1.set_title(f'Cost Comparison: {inst}')
    ax1.tick_params(axis='x', rotation=45)
    ax1.grid(axis='y', alpha=0.3)
    for bar, val in zip(bars, costs):
        ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 100, f'{val:.0f}', 
                ha='center', va='bottom', fontsize=9)
    
    ax2 = axes[1, 0]
    ax2.pie(costs, labels=methods, colors=colors, autopct='%1.1f%%', startangle=90)
    ax2.set_title(f'Cost Distribution: {inst}')
    
    ax3 = axes[1, 1]
    bars = ax3.barh(methods, costs, color=colors)
    ax3.set_xlabel('Cost')
    ax3.set_ylabel('Init Method')
    ax3.set_title(f'Cost by Init Method: {inst}')
    ax3.grid(axis='x', alpha=0.3)
    for bar, val in zip(bars, costs):
        ax3.text(bar.get_width() + 100, bar.get_y() + bar.get_height()/2, f'{val:.0f}', 
                ha='left', va='center', fontsize=9)
    
    ax4 = axes[2, 0]
    sorted_methods = sorted(zip(costs, methods, colors), key=lambda x: x[0])
    sorted_costs = [x[0] for x in sorted_methods]
    sorted_labels = [x[1] for x in sorted_methods]
    sorted_colors = [x[2] for x in sorted_methods]
    bars = ax4.bar(range(len(sorted_methods)), sorted_costs, color=sorted_colors)
    ax4.set_xticks(range(len(sorted_methods)))
    ax4.set_xticklabels(sorted_labels, rotation=45, ha='right')
    ax4.set_xlabel('Init Method (Ranked)')
    ax4.set_ylabel('Cost')
    ax4.set_title(f'Ranked Cost: {inst}')
    ax4.grid(axis='y', alpha=0.3)
    for bar, val in zip(bars, sorted_costs):
        ax4.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 100, f'{val:.0f}', 
                ha='center', va='bottom', fontsize=9)
    
    ax5 = axes[2, 1]
    route_data = [instance_routes[inst].get(method, 0) for method in methods]
    if any(route_data):
        bars = ax5.bar(methods, route_data, color=colors, edgecolor='black', linewidth=1.5)
        ax5.set_xlabel('Init Method')
        ax5.set_ylabel('Number of Routes')
        ax5.set_title(f'Routes by Init Method: {inst}')
        ax5.tick_params(axis='x', rotation=45)
        ax5.grid(axis='y', alpha=0.3)
        for bar, val in zip(bars, route_data):
            ax5.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1, f'{val}', 
                    ha='center', va='bottom', fontsize=10)
    else:
        ax5.text(0.5, 0.5, 'No route data', ha='center', va='center')
        ax5.set_title(f'Routes: {inst}')
    
    plt.suptitle(f'VRPTW Initialization Methods Comparison: {inst}', fontsize=14, fontweight='bold')
    plt.tight_layout()
    plt.savefig(f'init_methods_{inst}.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: init_methods_{inst}.png")

method_costs = defaultdict(list)
for d in data:
    method_costs[d['Method']].append(d['Cost'])

print("\n=== Summary Statistics ===")
print(f"{'Method':<25} {'Avg Cost':>12} {'Std Cost':>12} {'Min Cost':>12} {'Max Cost':>12}")
print("-" * 75)
for method in methods:
    costs = method_costs[method]
    print(f"{method:<25} {np.mean(costs):>12.2f} {np.std(costs):>12.2f} {np.min(costs):>12.2f} {np.max(costs):>12.2f}")
