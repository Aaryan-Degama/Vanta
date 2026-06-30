"""
generate_data.py

Synthetic IOB2-tagged training data generator for Vanta's query NER model.

Schema (locked, see ISSUE.md resolution):
    O           - everything else (noise: "give me", "pictures of", "in", scene words)
    B-SELF/I-SELF       - "me", "I", "myself" -> resolves to owner's entity_id
    B-PERSON/I-PERSON   - named person spans ("Rahul", "my sister", "Priya")
    B-RELATION/I-RELATION - co-presence predicates ("with", "and", "along with")

NOTE: no LOCATION/TIME tags by design. CLIP handles scene/location, file
metadata handles time. NER only resolves "who" so the graph layer
(entity_memberships / entity_relations) can be queried.

Output: data/train.jsonl, data/val.jsonl
Each line: {"tokens": [...], "tags": [...]}
"""

import json
import random
from pathlib import Path

random.seed(42)

# ---------------------------------------------------------------------------
# Vocabulary banks
# ---------------------------------------------------------------------------

SELF_WORDS = ["me", "myself", "I"]

# First names + relation-as-name phrases (multi-word PERSON spans matter —
# this is why we need IOB2 and not just IO tagging)
PERSON_NAMES = [
    "Rahul", "Priya", "Ananya", "Karan", "Sneha", "Arjun", "Divya", "Rohan",
    "Meera", "Vikram", "Pooja", "Aditya", "Kavya", "Nikhil", "Isha", "Varun",
    "Tanya", "Aryan", "Riya", "Siddharth",
]

PERSON_RELATION_PHRASES = [
    "my sister", "my brother", "my mom", "my dad", "my mother", "my father",
    "my best friend", "my friend", "my girlfriend", "my boyfriend",
    "my wife", "my husband", "my cousin", "my roommate", "my colleague",
    "my grandmother", "my grandfather", "my aunt", "my uncle", "my teacher",
]

# co-presence connectors -> RELATION span
RELATION_WORDS = ["with", "and", "along with", "together with", "alongside"]

# pure noise prefixes that wrap the query - these should map to O
INTENT_PREFIXES = [
    "give me", "show me", "find", "search for", "get me", "pull up",
    "can you find", "i want", "i need", "show",
]

MEDIA_NOUNS = ["pictures", "photos", "images", "pics", "photo", "picture", "image"]

OF_WORDS = ["of"]

# scene/location nouns - O tag, these are CLIP's job not NER's
SCENE_PLACES = [
    "Kedarnath", "the beach", "Goa", "the mountains", "Manali", "the park",
    "the office", "college", "the wedding", "the trek", "Paris", "the lake",
    "the concert", "the birthday party", "the trip", "Ladakh", "the temple",
    "the cafe", "the football match", "Diwali", "the snow", "the sunset",
    "the airport", "the campus", "graduation",
]

PREPOSITIONS = ["in", "at", "from", "near", "during"]

TIME_PHRASES = [
    "last year", "yesterday", "last week", "in 2023", "last month",
    "this summer", "on my birthday",
]


def tok(text):
    """Split a phrase into whitespace tokens."""
    return text.split()


def tag_span(n, label):
    """Return IOB2 tags for a span of n tokens with given label (e.g. 'PERSON')."""
    if n == 0:
        return []
    return [f"B-{label}"] + [f"I-{label}"] * (n - 1)


# ---------------------------------------------------------------------------
# Sentence builders -> each returns (tokens, tags)
# ---------------------------------------------------------------------------

def build_self_only():
    """'Give me pictures of me in Kedarnath' style."""
    prefix = tok(random.choice(INTENT_PREFIXES))
    media = tok(random.choice(MEDIA_NOUNS))
    of_ = tok(random.choice(OF_WORDS))
    self_w = tok(random.choice(SELF_WORDS))
    prep = tok(random.choice(PREPOSITIONS))
    place = tok(random.choice(SCENE_PLACES))

    tokens = prefix + media + of_ + self_w + prep + place
    tags = (
        ["O"] * len(prefix)
        + ["O"] * len(media)
        + ["O"] * len(of_)
        + tag_span(len(self_w), "SELF")
        + ["O"] * len(prep)
        + ["O"] * len(place)
    )
    return tokens, tags


def build_person_only():
    """'Show me photos of Rahul at the beach' style."""
    prefix = tok(random.choice(INTENT_PREFIXES))
    media = tok(random.choice(MEDIA_NOUNS))
    of_ = tok(random.choice(OF_WORDS))
    person_phrase = random.choice(PERSON_NAMES + PERSON_RELATION_PHRASES)
    person = tok(person_phrase)
    prep = tok(random.choice(PREPOSITIONS))
    place = tok(random.choice(SCENE_PLACES))

    tokens = prefix + media + of_ + person + prep + place
    tags = (
        ["O"] * len(prefix)
        + ["O"] * len(media)
        + ["O"] * len(of_)
        + tag_span(len(person), "PERSON")
        + ["O"] * len(prep)
        + ["O"] * len(place)
    )
    return tokens, tags


def build_self_with_person():
    """'Find pictures of me with Priya in Goa' style - the core graph-traversal case."""
    prefix = tok(random.choice(INTENT_PREFIXES))
    media = tok(random.choice(MEDIA_NOUNS))
    of_ = tok(random.choice(OF_WORDS))
    self_w = tok(random.choice(SELF_WORDS))
    rel = tok(random.choice(RELATION_WORDS))
    person_phrase = random.choice(PERSON_NAMES + PERSON_RELATION_PHRASES)
    person = tok(person_phrase)

    include_place = random.random() < 0.7
    tokens = prefix + media + of_ + self_w + rel + person
    tags = (
        ["O"] * len(prefix)
        + ["O"] * len(media)
        + ["O"] * len(of_)
        + tag_span(len(self_w), "SELF")
        + tag_span(len(rel), "RELATION")
        + tag_span(len(person), "PERSON")
    )
    if include_place:
        prep = tok(random.choice(PREPOSITIONS))
        place = tok(random.choice(SCENE_PLACES))
        tokens += prep + place
        tags += ["O"] * len(prep) + ["O"] * len(place)
    return tokens, tags


def build_person_with_person():
    """'Photos of Rahul and Priya at the wedding' style - two people, graph intersection."""
    media = tok(random.choice(MEDIA_NOUNS))
    of_ = tok(random.choice(OF_WORDS))
    p1 = tok(random.choice(PERSON_NAMES + PERSON_RELATION_PHRASES))
    rel = tok(random.choice(RELATION_WORDS))
    p2 = tok(random.choice(PERSON_NAMES + PERSON_RELATION_PHRASES))
    prep = tok(random.choice(PREPOSITIONS))
    place = tok(random.choice(SCENE_PLACES))

    tokens = media + of_ + p1 + rel + p2 + prep + place
    tags = (
        ["O"] * len(media)
        + ["O"] * len(of_)
        + tag_span(len(p1), "PERSON")
        + tag_span(len(rel), "RELATION")
        + tag_span(len(p2), "PERSON")
        + ["O"] * len(prep)
        + ["O"] * len(place)
    )
    return tokens, tags


def build_scene_only():
    """No entities at all - pure CLIP path. 'Pictures of the sunset at the beach'."""
    prefix = tok(random.choice(INTENT_PREFIXES)) if random.random() < 0.6 else []
    media = tok(random.choice(MEDIA_NOUNS))
    of_ = tok(random.choice(OF_WORDS)) if random.random() < 0.7 else []
    place = tok(random.choice(SCENE_PLACES))
    extra = []
    if random.random() < 0.3:
        extra = tok(random.choice(TIME_PHRASES))

    tokens = prefix + media + of_ + place + extra
    tags = ["O"] * len(tokens)
    return tokens, tags


def build_self_time():
    """'Show me pictures of me from last year' - SELF + time noise, no place."""
    prefix = tok(random.choice(INTENT_PREFIXES))
    media = tok(random.choice(MEDIA_NOUNS))
    of_ = tok(random.choice(OF_WORDS))
    self_w = tok(random.choice(SELF_WORDS))
    time_p = tok(random.choice(TIME_PHRASES))

    tokens = prefix + media + of_ + self_w + time_p
    tags = (
        ["O"] * len(prefix)
        + ["O"] * len(media)
        + ["O"] * len(of_)
        + tag_span(len(self_w), "SELF")
        + ["O"] * len(time_p)
    )
    return tokens, tags


def build_bare_name():
    """Minimal query: just a name, no other words. Tests short-sequence robustness."""
    person_phrase = random.choice(PERSON_NAMES + PERSON_RELATION_PHRASES)
    person = tok(person_phrase)
    return person, tag_span(len(person), "PERSON")


def build_three_people():
    """'Pictures of me, Rahul and Priya' style - multi-entity intersection."""
    media = tok(random.choice(MEDIA_NOUNS))
    of_ = tok(random.choice(OF_WORDS))
    self_w = tok(random.choice(SELF_WORDS))
    p1 = tok(random.choice(PERSON_NAMES))
    rel = tok(random.choice(RELATION_WORDS))
    p2 = tok(random.choice(PERSON_NAMES))

    tokens = media + of_ + self_w + [","] + p1 + rel + p2
    tags = (
        ["O"] * len(media)
        + ["O"] * len(of_)
        + tag_span(len(self_w), "SELF")
        + ["O"]  # comma
        + tag_span(len(p1), "PERSON")
        + tag_span(len(rel), "RELATION")
        + tag_span(len(p2), "PERSON")
    )
    return tokens, tags


BUILDERS = [
    (build_self_only, 0.18),
    (build_person_only, 0.18),
    (build_self_with_person, 0.22),
    (build_person_with_person, 0.14),
    (build_scene_only, 0.14),
    (build_self_time, 0.06),
    (build_bare_name, 0.04),
    (build_three_people, 0.04),
]


def sample_builder():
    r = random.random()
    cum = 0.0
    for fn, p in BUILDERS:
        cum += p
        if r <= cum:
            return fn
    return BUILDERS[-1][0]


def generate(n):
    seen = set()
    examples = []
    attempts = 0
    while len(examples) < n and attempts < n * 20:
        attempts += 1
        fn = sample_builder()
        tokens, tags = fn()
        key = " ".join(tokens).lower()
        if key in seen:
            continue
        seen.add(key)
        examples.append({"tokens": tokens, "tags": tags})
    return examples


def main():
    out_dir = Path(__file__).parent / "data"
    out_dir.mkdir(exist_ok=True)

    n_total = 6000
    examples = generate(n_total)
    random.shuffle(examples)

    n_val = max(200, int(len(examples) * 0.1))
    val = examples[:n_val]
    train = examples[n_val:]

    with open(out_dir / "train.jsonl", "w") as f:
        for ex in train:
            f.write(json.dumps(ex) + "\n")

    with open(out_dir / "val.jsonl", "w") as f:
        for ex in val:
            f.write(json.dumps(ex) + "\n")

    print(f"Generated {len(train)} train / {len(val)} val examples -> {out_dir}")
    print("\nSample examples:")
    for ex in train[:5]:
        print(" ", list(zip(ex["tokens"], ex["tags"])))


if __name__ == "__main__":
    main()
