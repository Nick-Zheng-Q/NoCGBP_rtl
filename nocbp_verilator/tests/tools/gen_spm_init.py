#!/usr/bin/env python3
"""
gen_spm_init.py
Generate SPM initialization data from graph + partition.

Input: partition JSON (from parse_bal_txt.py --partition)
Output: C++ header file with SPM init arrays, or hex files for $readmemh.

Usage:
  python3 gen_spm_init.py partition.json --output spm_init.h
  python3 gen_spm_init.py partition.json --output-hex spm_init_pe%d.hex
"""

import argparse
import json
import struct
import sys
from pathlib import Path


def float_to_u32(f: float) -> int:
    return struct.unpack('I', struct.pack('f', f))[0]


def encode_node_header(node_id: int, dof: int, adj_count: int,
                       adj_base: int, state_base: int, state_words: int) -> int:
    """Encode NodeHeader into 64-bit value matching RTL bit layout."""
    h = 0
    h |= (node_id & 0x3FF)
    h |= (dof & 0xF) << 10
    h |= (adj_count & 0xF) << 14
    h |= (adj_base & 0x3FFFF) << 18
    h |= (state_base & 0x3FFFF) << 36
    h |= (state_words & 0x1FF) << 54
    return h


def encode_adj_entry(neighbor_id: int, neighbor_x: int, neighbor_y: int) -> int:
    """Encode AdjEntry into 64-bit value."""
    e = 0
    e |= (neighbor_id & 0x3FF)
    e |= (neighbor_x & 0x3F) << 10
    e |= (neighbor_y & 0x1F) << 16
    return e


def compact_words(dof: int) -> int:
    return dof + dof * (dof + 1) // 2


def encode_state(dof: int, eta: list, lam_upper_tri: list) -> list:
    """Encode state into compact payload words."""
    words = []
    for i in range(dof):
        words.append(float_to_u32(eta[i]))
    for val in lam_upper_tri:
        words.append(float_to_u32(val))
    return words


def generate_spm_init(partition: dict, dataset: dict = None) -> dict:
    """Generate SPM initialization data for each PE.

    Returns: {pe_id: [(word_addr, data), ...]}
    """
    mesh_x = partition.get('mesh', {}).get('x', 2)
    mesh_y = partition.get('mesh', {}).get('y', 2)
    n_pes = partition.get('pes', mesh_x * mesh_y)

    var_mapping = partition.get('var_mapping_table', [])
    fac_mapping = partition.get('fac_mapping_table', [])
    edges = partition.get('factor_var_edges', [])
    n_var_nodes = partition.get('graph', {}).get('n_var_nodes', 0)
    n_fac_nodes = partition.get('graph', {}).get('n_fac_nodes', 0)

    # SPM layout constants (must match RTL)
    ADJ_BASE_ADDR = 0x100
    STATE_BASE_ADDR = 0x200

    spm_data = {pe: [] for pe in range(n_pes)}

    for pe in range(n_pes):
        pe_var_nodes = var_mapping[pe] if pe < len(var_mapping) else []
        pe_fac_nodes = fac_mapping[pe] if pe < len(fac_mapping) else []

        # Build adjacency for each node on this PE
        adj_offset = ADJ_BASE_ADDR
        state_offset = STATE_BASE_ADDR

        # Variable nodes
        for local_idx, node_id in enumerate(pe_var_nodes):
            dof = 2  # default, should come from dataset
            if dataset and 'variables' in dataset and node_id < len(dataset['variables']):
                dof = dataset['variables'][node_id].get('dof', 2)

            # Count edges for this node
            adj_count = 0
            for edge in edges:
                if len(edge) >= 2 and (edge[0] == node_id or edge[1] == node_id):
                    adj_count += 1

            cwords = compact_words(dof)
            header = encode_node_header(node_id, dof, adj_count,
                                        adj_offset, state_offset, cwords)
            spm_data[pe].append((node_id * 2, header & 0xFFFFFFFF))
            spm_data[pe].append((node_id * 2 + 1, (header >> 32) & 0xFFFFFFFF))

            # Write adj entries
            for edge in edges:
                if len(edge) >= 2 and (edge[0] == node_id or edge[1] == node_id):
                    neighbor_id = edge[1] if edge[0] == node_id else edge[0]
                    adj_entry = encode_adj_entry(neighbor_id, 0, 0)  # coordinates TBD
                    spm_data[pe].append((adj_offset, adj_entry & 0xFFFFFFFF))
                    spm_data[pe].append((adj_offset + 1, (adj_entry >> 32) & 0xFFFFFFFF))
                    adj_offset += 2

            # Write state (prior: eta + lambda identity)
            eta = [0.0] * dof
            lam = []
            for i in range(dof):
                for j in range(i, dof):
                    lam.append(1.0 if i == j else 0.0)

            if dataset and 'variables' in dataset and node_id < len(dataset['variables']):
                var_data = dataset['variables'][node_id]
                if 'eta' in var_data:
                    eta = var_data['eta'][:dof]
                if 'lambda' in var_data:
                    lam = var_data['lambda'][:len(lam)]

            state_words = encode_state(dof, eta, lam)
            for i, word in enumerate(state_words):
                spm_data[pe].append((state_offset + i, word))
            state_offset += cwords

        # Factor nodes
        for local_idx, node_id in enumerate(pe_fac_nodes):
            dof = 2  # default
            if dataset and 'factors' in dataset and node_id < len(dataset['factors']):
                dof = dataset['factors'][node_id].get('dof', 2)

            adj_count = 0
            for edge in edges:
                if len(edge) >= 2 and edge[0] == node_id:
                    adj_count += 1

            cwords = compact_words(dof)
            header = encode_node_header(node_id, dof, adj_count,
                                        adj_offset, state_offset, cwords)
            spm_data[pe].append((node_id * 2, header & 0xFFFFFFFF))
            spm_data[pe].append((node_id * 2 + 1, (header >> 32) & 0xFFFFFFFF))

            # Write adj entries for factor
            for edge in edges:
                if len(edge) >= 2 and edge[0] == node_id:
                    neighbor_id = edge[1]
                    adj_entry = encode_adj_entry(neighbor_id, 0, 0)
                    spm_data[pe].append((adj_offset, adj_entry & 0xFFFFFFFF))
                    spm_data[pe].append((adj_offset + 1, (adj_entry >> 32) & 0xFFFFFFFF))
                    adj_offset += 2

            # Write factor state (placeholder zeros for now)
            state_words = [0] * cwords
            for i, word in enumerate(state_words):
                spm_data[pe].append((state_offset + i, word))
            state_offset += cwords

    return spm_data


def write_c_header(spm_data: dict, output_path: str):
    """Write C++ header file with SPM init arrays."""
    lines = [
        "// Auto-generated by gen_spm_init.py",
        "#pragma once",
        "#include <cstdint>",
        "",
        "struct SpmInitEntry {",
        "    uint32_t word_addr;",
        "    uint32_t data;",
        "};",
        "",
    ]

    for pe_id, entries in sorted(spm_data.items()):
        var_name = f"spm_init_pe{pe_id}"
        lines.append(f"static const SpmInitEntry {var_name}[] = {{")
        for addr, data in entries:
            lines.append(f"    {{0x{addr:04X}, 0x{data:08X}}},")
        lines.append(f"}};")
        lines.append(f"static const int {var_name}_count = {len(entries)};")
        lines.append("")

    Path(output_path).write_text('\n'.join(lines))
    print(f"Written C++ header to {output_path}")


def write_hex_files(spm_data: dict, output_pattern: str):
    """Write hex files for $readmemh."""
    for pe_id, entries in sorted(spm_data.items()):
        path = output_pattern % pe_id
        lines = []
        for addr, data in entries:
            lines.append(f"@{addr:04X} {data:08X}")
        Path(path).write_text('\n'.join(lines) + '\n')
        print(f"Written hex file to {path}")


def main():
    parser = argparse.ArgumentParser(description="Generate SPM init data")
    parser.add_argument("partition", help="Partition JSON file")
    parser.add_argument("--dataset", help="Dataset JSON file (optional)")
    parser.add_argument("--output", "-o", help="Output C++ header file")
    parser.add_argument("--output-hex", help="Output hex file pattern (e.g., spm_init_pe%d.hex)")
    args = parser.parse_args()

    partition = json.loads(Path(args.partition).read_text())
    dataset = None
    if args.dataset:
        dataset = json.loads(Path(args.dataset).read_text())

    spm_data = generate_spm_init(partition, dataset)

    total_words = sum(len(v) for v in spm_data.values())
    print(f"Generated {total_words} words for {len(spm_data)} PEs")

    if args.output:
        write_c_header(spm_data, args.output)
    if args.output_hex:
        write_hex_files(spm_data, args.output_hex)
    if not args.output and not args.output_hex:
        # Default: print summary
        for pe_id, entries in sorted(spm_data.items()):
            print(f"  PE {pe_id}: {len(entries)} words")


if __name__ == "__main__":
    main()
