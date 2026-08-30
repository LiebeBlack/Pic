"""
Punto de entrada compatible para ARTPICST Updater.
Reenvía la ejecución al motor central en auto_updater.py.
"""

import sys
from auto_updater import main

if __name__ == "__main__":
    sys.exit(main())
