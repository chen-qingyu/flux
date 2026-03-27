import argparse
import time

import flux


if __name__ == "__main__":
    parser = argparse.ArgumentParser("flux")
    parser.add_argument(
        "--file",
        required=True,
        help="Path to the BPMN model to simulate.",
    )
    parser.add_argument(
        "--seed",
        default=42,
        type=int,
        help="Deterministic random seed used by the simulator.",
    )
    args = parser.parse_args()
    start_time = time.time()
    flux.run(args.file, args.seed)
    end_time = time.time()
    print(f"Used time: {end_time - start_time:.3f} s.")
