# quantisized_model

Training pipeline for the SIBOL grading tree. Produces the flat float array
that `SIBOL_NODE.ino` evaluates on-chip.

## Files

| File | Purpose |
|---|---|
| `seed_demo.csv` | 20 generated demo rows, tagged `source=sim` |
| `train_model.py` | trains CART, exports C header + params + report |
| `sibol_model.h` | generated — drop-in array for the firmware |
| `params.json` | generated — node parameters for the prototype build |
| `model_report.txt` | generated — metrics, confusion matrix, readable rules |

The three generated files are produced by the script. Delete them and re-run to
prove the pipeline reproduces them.

## Run

```bash
pip install pandas scikit-learn numpy
cd quantisized_model
python train_model.py