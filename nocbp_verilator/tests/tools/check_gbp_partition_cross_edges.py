#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
from typing import cast


ROOT = Path(__file__).resolve().parents[3]


def load_mapping(path_arg: str) -> dict[str, object]:
    mapping_path = (ROOT / path_arg).resolve()
    payload = cast(object, json.loads(mapping_path.read_text(encoding="utf-8")))
    if not isinstance(payload, dict):
        raise ValueError("mapping JSON root must be an object")
    payload_dict = cast(dict[object, object], payload)
    mapping: dict[str, object] = {}
    for key, value in payload_dict.items():
        if not isinstance(key, str):
            raise ValueError("mapping JSON keys must be strings")
        mapping[key] = value
    return mapping


def parse_mapping_table(table_obj: object, pes: int, table_name: str) -> list[list[int]]:
    if not isinstance(table_obj, list):
        raise ValueError(f"{table_name} must be a list")
    raw_table = cast(list[object], table_obj)
    if len(raw_table) != pes:
        raise ValueError(f"{table_name} must contain {pes} PE buckets")

    table: list[list[int]] = []
    for bucket in raw_table:
        if not isinstance(bucket, list):
            raise ValueError(f"{table_name} bucket must be a list")
        raw_bucket = cast(list[object], bucket)
        nodes: list[int] = []
        for node in raw_bucket:
            if not isinstance(node, int):
                raise ValueError(f"{table_name} node ids must be integers")
            nodes.append(node)
        table.append(nodes)
    return table


def build_owners(table: list[list[int]], count: int, node_name: str) -> list[int]:
    owners = [-1] * count
    for pe_id, nodes in enumerate(table):
        for node_id in nodes:
            if node_id < 0 or node_id >= count:
                raise ValueError(f"{node_name} id out of range: {node_id}")
            if owners[node_id] != -1:
                raise ValueError(f"{node_name} id appears on multiple PEs: {node_id}")
            owners[node_id] = pe_id
    if any(owner < 0 for owner in owners):
        raise ValueError(f"{node_name} ownership incomplete")
    return owners


def validate_cross_edges(mapping: dict[str, object]) -> tuple[int, int]:
    pes_obj = mapping.get("pes")
    if not isinstance(pes_obj, int) or pes_obj <= 0:
        raise ValueError("invalid or missing pes")
    pes = pes_obj

    fac_table = parse_mapping_table(mapping.get("fac_mapping_table"), pes, "fac_mapping_table")
    var_table = parse_mapping_table(mapping.get("var_mapping_table"), pes, "var_mapping_table")

    graph_obj = mapping.get("graph")
    if not isinstance(graph_obj, dict):
        raise ValueError("graph section missing")
    graph = cast(dict[object, object], graph_obj)

    n_fac_obj = graph.get("n_fac_nodes")
    n_var_obj = graph.get("n_var_nodes")
    edges_obj = graph.get("factor_var_edges")
    if not isinstance(n_fac_obj, int) or n_fac_obj < 0:
        raise ValueError("graph.n_fac_nodes missing or invalid")
    if not isinstance(n_var_obj, int) or n_var_obj < 0:
        raise ValueError("graph.n_var_nodes missing or invalid")
    if not isinstance(edges_obj, list):
        raise ValueError("graph.factor_var_edges missing or invalid")
    n_fac = n_fac_obj
    n_var = n_var_obj

    fac_owners = build_owners(fac_table, n_fac, "factor")
    var_owners = build_owners(var_table, n_var, "variable")

    total_edges = 0
    cross_edges = 0
    for edge_obj in cast(list[object], edges_obj):
        if not isinstance(edge_obj, list):
            raise ValueError("edge entries must be [factor_id, variable_id]")
        edge = cast(list[object], edge_obj)
        if len(edge) != 2:
            raise ValueError("edge entries must be [factor_id, variable_id]")
        factor_id_obj = edge[0]
        variable_id_obj = edge[1]
        if not isinstance(factor_id_obj, int) or not isinstance(variable_id_obj, int):
            raise ValueError("edge ids must be integers")
        factor_id = factor_id_obj
        variable_id = variable_id_obj
        if factor_id < 0 or factor_id >= n_fac:
            raise ValueError(f"edge factor id out of range: {factor_id}")
        if variable_id < 0 or variable_id >= n_var:
            raise ValueError(f"edge variable id out of range: {variable_id}")
        total_edges += 1
        if fac_owners[factor_id] != var_owners[variable_id]:
            cross_edges += 1
    return total_edges, cross_edges


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate exported partition has cross-PE edges")
    _ = parser.add_argument("--mapping", required=True)
    args = parser.parse_args()
    args_dict = vars(args)
    mapping_path_obj = args_dict.get("mapping")
    if not isinstance(mapping_path_obj, str):
        raise TypeError("--mapping must be a string")

    mapping = load_mapping(mapping_path_obj)
    total_edges, cross_edges = validate_cross_edges(mapping)
    if cross_edges <= 0:
        raise SystemExit("ERROR: no cross-PE factor-variable edges found")

    print(f"mapping: {mapping_path_obj}")
    print(f"total_factor_variable_edges: {total_edges}")
    print(f"cross_pe_edges: {cross_edges}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
