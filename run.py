import argparse
import time
from pathlib import Path

import flux


if __name__ == "__main__":
    parser = argparse.ArgumentParser("flux")
    parser.add_argument(
        "file",
        help="Path to the BPMN file to simulate.",
    )
    parser.add_argument(
        "--seed",
        default=42,
        type=int,
        help="Deterministic random seed used by the simulator.",
    )
    parser.add_argument(
        "--output",
        default="output",
        help="Directory to write CSV reports into.",
    )
    args = parser.parse_args()
    with open(args.file, encoding="utf-8") as f:
        content = f.read()
    start_time = time.time()
    flux.run(Path(args.file).stem, content, output_dir=args.output, random_seed=args.seed)
    end_time = time.time()
    print(f"Used time: {end_time - start_time:.3f} s.")
