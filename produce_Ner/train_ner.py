"""
train_ner.py

Fine-tunes google/bert_uncased_L-2_H-128_A-2 (BERT-tiny) for token
classification on Vanta's query NER schema, then exports to ONNX for
on-device Android inference.

Schema: O, B-SELF, I-SELF, B-PERSON, I-PERSON, B-RELATION, I-RELATION

Usage:
    python generate_data.py          # produces data/train.jsonl, data/val.jsonl
    python train_ner.py              # trains + exports

Output:
    ./ner_checkpoint/                 - HF checkpoint (best val F1)
    ./onnx_model/ner_bert_tiny.onnx   - ONNX export for Android
    ./onnx_model/label_map.json       - id2label / label2id for the C++ side
"""

import json
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import Dataset
from transformers import (
    AutoTokenizer,
    AutoModelForTokenClassification,
    TrainingArguments,
    Trainer,
    DataCollatorForTokenClassification,
)
from seqeval.metrics import f1_score, precision_score, recall_score, classification_report

MODEL_NAME = "google/bert_uncased_L-2_H-128_A-2"
DATA_DIR = Path(__file__).parent / "data"
CHECKPOINT_DIR = Path(__file__).parent / "ner_checkpoint"
ONNX_DIR = Path(__file__).parent / "onnx_model"
MAX_LEN = 32  # queries are short; generous headroom over wordpiece expansion

LABELS = [
    "O",
    "B-SELF", "I-SELF",
    "B-PERSON", "I-PERSON",
    "B-RELATION", "I-RELATION",
]
LABEL2ID = {l: i for i, l in enumerate(LABELS)}
ID2LABEL = {i: l for i, l in enumerate(LABELS)}


# ---------------------------------------------------------------------------
# Dataset
# ---------------------------------------------------------------------------

def load_jsonl(path):
    examples = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                examples.append(json.loads(line))
    return examples


def align_labels_with_tokens(word_ids, word_tags):
    """
    Wordpiece tokenization splits one word into multiple sub-tokens.
    Only the first sub-token of a word gets the real label; continuation
    sub-tokens get -100 (ignored by the loss). Special tokens ([CLS]/[SEP])
    also get -100.
    """
    label_ids = []
    prev_word_id = None
    for word_id in word_ids:
        if word_id is None:
            label_ids.append(-100)
        elif word_id != prev_word_id:
            label_ids.append(LABEL2ID[word_tags[word_id]])
        else:
            label_ids.append(-100)
        prev_word_id = word_id
    return label_ids


class NERDataset(Dataset):
    def __init__(self, examples, tokenizer, max_len=MAX_LEN):
        self.examples = examples
        self.tokenizer = tokenizer
        self.max_len = max_len

    def __len__(self):
        return len(self.examples)

    def __getitem__(self, idx):
        ex = self.examples[idx]
        tokens, tags = ex["tokens"], ex["tags"]

        encoding = self.tokenizer(
            tokens,
            is_split_into_words=True,
            truncation=True,
            max_length=self.max_len,
            padding="max_length",
        )
        word_ids = encoding.word_ids()
        labels = align_labels_with_tokens(word_ids, tags)

        item = {k: torch.tensor(v) for k, v in encoding.items()}
        item["labels"] = torch.tensor(labels)
        return item


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------

def compute_metrics(eval_pred):
    predictions, labels = eval_pred
    predictions = np.argmax(predictions, axis=2)

    true_predictions = [
        [ID2LABEL[p] for (p, l) in zip(pred_row, label_row) if l != -100]
        for pred_row, label_row in zip(predictions, labels)
    ]
    true_labels = [
        [ID2LABEL[l] for (p, l) in zip(pred_row, label_row) if l != -100]
        for pred_row, label_row in zip(predictions, labels)
    ]

    return {
        "precision": precision_score(true_labels, true_predictions),
        "recall": recall_score(true_labels, true_predictions),
        "f1": f1_score(true_labels, true_predictions),
    }


# ---------------------------------------------------------------------------
# Training
# ---------------------------------------------------------------------------

def train():
    train_examples = load_jsonl(DATA_DIR / "train.jsonl")
    val_examples = load_jsonl(DATA_DIR / "val.jsonl")
    print(f"Loaded {len(train_examples)} train / {len(val_examples)} val examples")

    tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
    model = AutoModelForTokenClassification.from_pretrained(
        MODEL_NAME,
        num_labels=len(LABELS),
        id2label=ID2LABEL,
        label2id=LABEL2ID,
    )

    train_ds = NERDataset(train_examples, tokenizer)
    val_ds = NERDataset(val_examples, tokenizer)
    collator = DataCollatorForTokenClassification(tokenizer)

    args = TrainingArguments(
        output_dir=str(CHECKPOINT_DIR / "runs"),
        eval_strategy="epoch",
        save_strategy="epoch",
        learning_rate=5e-5,
        per_device_train_batch_size=64,
        per_device_eval_batch_size=64,
        num_train_epochs=15,
        weight_decay=0.01,
        load_best_model_at_end=True,
        metric_for_best_model="f1",
        greater_is_better=True,
        logging_steps=20,
        save_total_limit=2,
        report_to="none",
    )

    trainer = Trainer(
        model=model,
        args=args,
        train_dataset=train_ds,
        eval_dataset=val_ds,
        data_collator=collator,
        compute_metrics=compute_metrics,
    )

    trainer.train()

    print("\n=== Final validation report ===")
    preds = trainer.predict(val_ds)
    predictions = np.argmax(preds.predictions, axis=2)
    labels = preds.label_ids
    true_predictions = [
        [ID2LABEL[p] for (p, l) in zip(pred_row, label_row) if l != -100]
        for pred_row, label_row in zip(predictions, labels)
    ]
    true_labels = [
        [ID2LABEL[l] for (p, l) in zip(pred_row, label_row) if l != -100]
        for pred_row, label_row in zip(predictions, labels)
    ]
    print(classification_report(true_labels, true_predictions))

    CHECKPOINT_DIR.mkdir(exist_ok=True)
    trainer.save_model(str(CHECKPOINT_DIR))
    tokenizer.save_pretrained(str(CHECKPOINT_DIR))
    print(f"\nSaved best checkpoint to {CHECKPOINT_DIR}")

    return model, tokenizer


# ---------------------------------------------------------------------------
# ONNX export - mirrors the conventions in produce_model/get_onnx_CLIP.py
# (opset 18, dynamic batch axis, exported to ./onnx_model/)
# ---------------------------------------------------------------------------

def export_onnx():
    ONNX_DIR.mkdir(exist_ok=True)

    tokenizer = AutoTokenizer.from_pretrained(str(CHECKPOINT_DIR))
    model = AutoModelForTokenClassification.from_pretrained(str(CHECKPOINT_DIR))
    model.eval()

    dummy = tokenizer(
        ["give", "me", "pictures", "of", "me", "with", "Priya"],
        is_split_into_words=True,
        return_tensors="pt",
        padding="max_length",
        max_length=MAX_LEN,
    )

    torch.onnx.export(
        model,
        (dummy["input_ids"], dummy["attention_mask"], dummy["token_type_ids"]),
        str(ONNX_DIR / "ner_bert_tiny.onnx"),
        export_params=True,
        opset_version=18,
        do_constant_folding=True,
        input_names=["input_ids", "attention_mask", "token_type_ids"],
        output_names=["logits"],
        dynamic_axes={
            "input_ids": {0: "batch_size"},
            "attention_mask": {0: "batch_size"},
            "token_type_ids": {0: "batch_size"},
            "logits": {0: "batch_size"},
        },
    )

    # Save tokenizer vocab + label map alongside the model. The C++ side
    # (Query/NER_BERT) needs vocab.txt for wordpiece tokenization and
    # label_map.json to decode logits back into BIO tags.
    tokenizer.save_pretrained(str(ONNX_DIR))
    with open(ONNX_DIR / "label_map.json", "w") as f:
        json.dump({"id2label": ID2LABEL, "label2id": LABEL2ID, "max_len": MAX_LEN}, f, indent=2)

    print(f"Exported ONNX model + tokenizer + label_map.json to {ONNX_DIR}")


def sanity_check_onnx():
    """Quick parity check: HF model vs ONNX Runtime output on one example."""
    import onnxruntime as ort

    tokenizer = AutoTokenizer.from_pretrained(str(CHECKPOINT_DIR))
    model = AutoModelForTokenClassification.from_pretrained(str(CHECKPOINT_DIR))
    model.eval()

    tokens = ["pictures", "of", "me", "with", "Rahul", "in", "Goa"]
    enc = tokenizer(
        tokens, is_split_into_words=True, return_tensors="pt",
        padding="max_length", max_length=MAX_LEN,
    )

    with torch.no_grad():
        torch_logits = model(**enc).logits.numpy()

    sess = ort.InferenceSession(str(ONNX_DIR / "ner_bert_tiny.onnx"))
    onnx_logits = sess.run(
        ["logits"],
        {
            "input_ids": enc["input_ids"].numpy(),
            "attention_mask": enc["attention_mask"].numpy(),
            "token_type_ids": enc["token_type_ids"].numpy(),
        },
    )[0]

    max_diff = np.abs(torch_logits - onnx_logits).max()
    print(f"Max logit diff (torch vs onnx): {max_diff:.6f}")
    assert max_diff < 1e-3, "ONNX export diverges from PyTorch model!"

    word_ids = enc.word_ids()
    pred_ids = np.argmax(onnx_logits[0], axis=-1)
    seen = set()
    decoded = []
    for wid, pid in zip(word_ids, pred_ids):
        if wid is None or wid in seen:
            continue
        seen.add(wid)
        decoded.append((tokens[wid], ID2LABEL[pid]))
    print("Decoded:", decoded)


if __name__ == "__main__":
    train()
    export_onnx()
    sanity_check_onnx()
