import sys
import torch
import numpy as np
from transformers import AutoTokenizer, AutoModelForTokenClassification
from pathlib import Path
import time

CHECKPOINT_DIR = Path(__file__).parent / "ner_checkpoint"
MAX_LEN = 32

def main():
    if not CHECKPOINT_DIR.exists():
        print(f"Error: {CHECKPOINT_DIR} not found. Please train the model first by running train_ner.py")
        sys.exit(1)

    print(f"Loading model from {CHECKPOINT_DIR}...")
    tokenizer = AutoTokenizer.from_pretrained(str(CHECKPOINT_DIR))
    model = AutoModelForTokenClassification.from_pretrained(str(CHECKPOINT_DIR))
    model.eval()

    print("Model loaded successfully.\n")

    start_time = time.time()
    # If arguments are provided, use them as the input query
    if len(sys.argv) > 1:
        sentences = [" ".join(sys.argv[1:])]
    else:
        # Default test sentences
        sentences = [
            "pictures of me with Priya in Goa",
            "show me photos of Rahul and my mom",
            "images of a dog in the park",
            "selfies with my brother"
        ]

    for text in sentences:
        print(f"Input: '{text}'")
        tokens = text.split() 
        
        enc = tokenizer(
            tokens, is_split_into_words=True, return_tensors="pt",
            padding="max_length", max_length=MAX_LEN,
            truncation=True
        )

        with torch.no_grad():
            outputs = model(**enc)
            logits = outputs.logits.numpy()

        pred_ids = np.argmax(logits[0], axis=-1)
        word_ids = enc.word_ids()

        seen = set()
        decoded = []
        for wid, pid in zip(word_ids, pred_ids):
            if wid is None or wid in seen:
                continue
            seen.add(wid)
            label = model.config.id2label[pid]
            
            # Format the output word based on the predicted tag
            if label != "O":
                decoded.append(f"\033[92m{tokens[wid]}[{label}]\033[0m")
            else:
                decoded.append(tokens[wid])
        
        print("Output:", " ".join(decoded))
        print("-" * 50)

    end_time = time.time()
    print(f"Total time: {end_time - start_time} seconds")

if __name__ == "__main__":
    main()
