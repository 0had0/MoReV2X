"""Tiny notebook builder: md() and code() cells, save() writes an nbformat 4 notebook."""
import json


class NB:
    def __init__(self):
        self.cells = []

    def md(self, text):
        self.cells.append({"cell_type": "markdown", "metadata": {}, "source": text.strip("\n")})

    def code(self, text):
        self.cells.append({"cell_type": "code", "metadata": {}, "execution_count": None, "outputs": [],
                           "source": text.strip("\n")})

    def save(self, path):
        notebook = {"cells": self.cells, "nbformat": 4, "nbformat_minor": 5,
                    "metadata": {"kernelspec": {"display_name": "Python 3", "language": "python", "name": "python3"},
                                 "language_info": {"name": "python"}}}
        json.dump(notebook, open(path, "w"), indent=1)


SETUP = '''
import os, sys, json, subprocess
sys.path.insert(0, os.path.abspath("../../tools"))
sys.path.insert(0, os.path.abspath("../../tools/qos"))
import morev2x_results as mr
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import yaml

mr.apply_chart_style()
os.chdir(mr.ROOT)
pd.set_option("display.max_colwidth", 80)
pd.set_option("display.width", 160)
'''
