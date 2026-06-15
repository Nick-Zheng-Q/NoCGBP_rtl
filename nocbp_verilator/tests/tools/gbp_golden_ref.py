#!/usr/bin/env python3
"""
GBP Golden Reference (Python)
Simplified numerical model for GBP belief propagation.

For Phase 1 (Direction C), supports:
  - Local-only variable nodes (identity/pass-through)
  - 4-node chain / mesh topologies (local edges only)

State format per node (DOF=1):
  {
    "eta": float,     # information vector (1x1)
    "lambda": float   # information matrix (1x1, scalar precision)
  }

For local-only nodes with no incoming messages, belief update is identity:
  eta'   = eta
  lambda' = lambda
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def run_local_identity(nodes: list[dict]) -> list[dict]:
    """Local-only nodes: belief is unchanged (identity)."""
    return [dict(n) for n in nodes]


def run_iteration(nodes: list[dict], edges: list[dict]) -> list[dict]:
    """
    Run one GBP synchronous iteration.

    For local-only nodes (no edges), this is identity.
    When messages are present, this computes:
      eta'   = prior_eta   + sum(message_eta)
      lambda' = prior_lambda + sum(message_lambda)
    """
    if not edges:
        return run_local_identity(nodes)

    # Build adjacency
    adj: dict[int, list[int]] = {n["id"]: [] for n in nodes}
    for e in edges:
        src = e["src"]
        dst = e["dst"]
        adj[src].append(dst)
        adj[dst].append(src)

    new_nodes = []
    for node in nodes:
        nid = node["id"]
        eta = node["state"]["eta"]
        lam = node["state"]["lambda"]

        # Accumulate messages from adjacent nodes
        # For now, messages are identity (placeholder for full factor computation)
        # When full factor support is added, messages will be computed from
        # factor node precision models.
        for neighbor_id in adj.get(nid, []):
            neighbor = next(n for n in nodes if n["id"] == neighbor_id)
            # Simplified message: just pass through neighbor's state
            # (This is a placeholder; real GBP computes factor-to-variable messages)
            eta += neighbor["state"]["eta"]
            lam += neighbor["state"]["lambda"]

        new_nodes.append({
            "id": nid,
            "pe": node.get("pe", 0),
            "dof": node.get("dof", 1),
            "state": {"eta": eta, "lambda": lam}
        })

    return new_nodes


def run_gbp(config: dict, num_rounds: int = 1) -> dict:
    """Run GBP golden reference for given rounds."""
    nodes = [dict(n) for n in config["nodes"]]
    edges = config.get("edges", [])

    history = [nodes]
    for _ in range(num_rounds):
        nodes = run_iteration(nodes, edges)
        history.append(nodes)

    return {
        "schema_version": "1.0",
        "artifact_type": "gbp_golden_reference",
        "config": config,
        "num_rounds": num_rounds,
        "final_state": nodes,
        "history": history,
    }


def load_config(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    return json.loads(text)


def main() -> int:
    parser = argparse.ArgumentParser(description="GBP Golden Reference")
    parser.add_argument("--config", required=True, help="Input JSON config path")
    parser.add_argument("--rounds", type=int, default=1, help="Number of GBP rounds")
    parser.add_argument("--output", required=True, help="Output JSON path")
    args = parser.parse_args()

    config = load_config(Path(args.config))
    result = run_gbp(config, args.rounds)

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"Golden reference written to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
