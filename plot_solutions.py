import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import os
import sqlite3
import json

def parse_instance(filepath):
    customers = {}
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    in_customer_section = False
    for line in lines:
        line = line.strip()
        if line == 'CUSTOMER':
            in_customer_section = True
            continue
        if in_customer_section and line:
            parts = line.split()
            if len(parts) >= 3 and parts[0].isdigit():
                cust_id = int(parts[0])
                x = float(parts[1])
                y = float(parts[2])
                customers[cust_id] = (x, y)
    return customers

def parse_solution(filepath):
    routes = []
    with open(filepath, 'r') as f:
        for line in f:
            if line.startswith('Route'):
                parts = line.split(':')
                if len(parts) >= 2:
                    route_str = parts[1].strip()
                    if route_str:
                        customers = [int(x) for x in route_str.split()]
                        routes.append(customers)
    return routes

def load_from_db(instance_name):
    try:
        conn = sqlite3.connect('optimization_results.db')
        cursor = conn.cursor()
        cursor.execute("""
            SELECT customers, routes, final_score 
            FROM optimization_stats 
            WHERE instance_name = ?
            ORDER BY id DESC
            LIMIT 1
        """, (instance_name.upper(),))
        row = cursor.fetchone()
        conn.close()
        if row:
            customers = json.loads(row[0])
            routes = json.loads(row[1])
            score = row[2]
            customer_dict = {c['id']: (c['x'], c['y']) for c in customers}
            return customer_dict, routes, score
    except Exception as e:
        print(f"Error loading from DB: {e}")
    return None, None, None

def euclidean_distance(p1, p2):
    return np.sqrt((p1[0] - p2[0])**2 + (p1[1] - p2[1])**2)

def plot_instance_with_solution(instance_name, customers, routes, output_path, score=None, source="file"):
    if not customers:
        print(f"No customers found for {instance_name}")
        return
    
    max_x = max(c[0] for c in customers.values())
    max_y = max(c[1] for c in customers.values())
    min_x = min(c[0] for c in customers.values())
    min_y = min(c[1] for c in customers.values())
    
    depot = customers.get(0, None)
    
    fig, ax = plt.subplots(1, 1, figsize=(12, 10))
    
    all_cust_ids = [c for c in customers.keys() if c != 0]
    x_coords = [customers[c][0] for c in all_cust_ids]
    y_coords = [customers[c][1] for c in all_cust_ids]
    ax.scatter(x_coords, y_coords, c='steelblue', s=50, alpha=0.6, label='Customers', zorder=3)
    
    for cust_id in all_cust_ids:
        ax.annotate(str(cust_id), customers[cust_id], fontsize=6, ha='center', va='bottom', alpha=0.7)
    
    if depot:
        ax.scatter(depot[0], depot[1], c='red', s=200, marker='*', edgecolors='black', linewidths=1, label='Depot', zorder=5)
        ax.annotate('Depot', depot, fontsize=8, ha='right', va='top', color='red')
    
    cmap = matplotlib.colormaps['tab10']
    
    total_distance = 0
    
    for route_idx, route in enumerate(routes):
        if not route:
            continue
        
        color = cmap(route_idx % 10)
        
        route_points = [0] + route + [0]
        
        for i in range(len(route_points) - 1):
            p1 = customers.get(route_points[i], None)
            p2 = customers.get(route_points[i + 1], None)
            if p1 and p2:
                ax.plot([p1[0], p2[0]], [p1[1], p2[1]], color=color, linewidth=1.5, alpha=0.7)
                total_distance += euclidean_distance(p1, p2)
        
        for cust_id in route:
            if cust_id in customers:
                ax.scatter(customers[cust_id][0], customers[cust_id][1], c=[color], s=80, edgecolors='black', linewidths=0.5, zorder=4)
    
    ax.set_xlabel('X Coordinate')
    ax.set_ylabel('Y Coordinate')
    if score is not None:
        title = f'VRPTW Solution: {instance_name} ({source})\n{len(routes)} Routes, Total Distance: {total_distance:.2f}, Score: {score:.2f}'
    else:
        title = f'VRPTW Solution: {instance_name} ({source})\n{len(routes)} Routes, Total Distance: {total_distance:.2f}'
    ax.set_title(title)
    ax.legend(loc='upper right')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(min_x - 5, max_x + 5)
    ax.set_ylim(min_y - 5, max_y + 5)
    ax.set_aspect('equal')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path} (Distance: {total_distance:.2f})")

instances_dir = 'benchmark_instances'
solutions_dir = 'benchmark-solutions'
output_dir = 'solution_plots'

os.makedirs(output_dir, exist_ok=True)

conn = sqlite3.connect('optimization_results.db')
cursor = conn.cursor()
cursor.execute("SELECT DISTINCT instance_name FROM optimization_stats")
db_instances = [row[0].lower() for row in cursor.fetchall()]
conn.close()

print(f"Instances in DB: {db_instances}")

instance_files = sorted([f for f in os.listdir(instances_dir) if f.endswith('.txt')])

for instance_file in instance_files:
    instance_name = instance_file.replace('.txt', '')
    instance_path = os.path.join(instances_dir, instance_file)
    
    db_customers, db_routes, db_score = load_from_db(instance_name)
    
    if db_customers and db_routes:
        customers = db_customers
        routes = db_routes
        score = db_score
        source = "DB"
    else:
        solution_path_candidates = [
            os.path.join(solutions_dir, f'{instance_name.upper()}.txt'),
            os.path.join(solutions_dir, f'{instance_name.lower()}.txt'),
        ]
        
        solution_path = None
        for p in solution_path_candidates:
            if os.path.exists(p):
                solution_path = p
                break
        
        if solution_path is None:
            print(f"No solution found for {instance_name}, skipping...")
            continue
        
        customers = parse_instance(instance_path)
        routes = parse_solution(solution_path)
        score = None
        source = "file"
    
    output_path = os.path.join(output_dir, f'{instance_name}_solution.png')
    plot_instance_with_solution(instance_name, customers, routes, output_path, score, source)

print(f"\nAll plots saved to {output_dir}/")
