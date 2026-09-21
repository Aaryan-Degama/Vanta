# 19 — Privacy, Security & Fairness

## Part 1 — What "privacy-first" actually means here

### The verifiable claim

**Vanta makes no network calls.** [VERIFIED-REPO]

- No HTTP client, no socket, no `fetch`, no `OkHttp`, no `URLConnection` in any Kotlin or C++ source.
- No `INTERNET` permission in the module manifest.
- No analytics SDK, no crash reporter, no telemetry.
- `package.json` has no networking dependency.
- All three models are bundled in the APK and extracted locally; the only downloads happen in
  `download_models.py` / `produce_model/`, which are **developer-time** scripts, not app code.

That is a genuinely strong claim and it is **architecturally enforced**, not policy-enforced: a C++ engine with no
socket code and an app with no `INTERNET` permission *cannot* exfiltrate data, regardless of intent or bugs.

> **The strongest framing:** *"The privacy property isn't a promise, it's a consequence of the architecture. The app
> doesn't request the `INTERNET` permission, so even a compromised dependency has no egress. That's the whole reason
> the project is hard — every design constraint downstream (a 4 M-parameter NER model, INT8 face weights,
> brute-force KNN, no cloud reranker, no labelled training data) exists because I refused the network."*

### What that buys, precisely

| Threat | Mitigated? |
|---|---|
| Cloud provider reads your photos | ✅ eliminated |
| Server breach leaks face embeddings | ✅ eliminated |
| Government subpoena to the service operator | ✅ there is no operator |
| Training on user data | ✅ impossible |
| Network interception | ✅ nothing transits |
| **Device compromise** | ❌ **not mitigated at all** |
| **Physical device access** | ❌ |
| **Malicious app on the same device** | ⚠️ partially, by Android sandboxing |
| **Backup exfiltration** | ❌ see below |

---

## Part 2 — Why "on-device" ≠ "secure"

This is the section that separates a thoughtful answer from a marketing one.

### 2.1 The database is unencrypted plaintext

`vanta.db` sits at `getDatabasePath("vanta.db")` → `/data/data/com.aaryan_ka.VantaApp/databases/vanta.db`, in the
app-private directory, **with no encryption**. Protection comes entirely from Android's filesystem sandbox (UID
isolation) plus full-disk / file-based encryption when the device is locked.

It is readable by: root, a custom recovery, an unlocked bootloader with `adb backup` enabled, anyone with the
unlocked device and developer access, and any exploit that escapes the sandbox.

**And it contains more than photo metadata:**

| Data | Sensitivity |
|---|---|
| Absolute paths of every photo, video, audio file and document on the device | 🟠 A complete map of the user's media |
| 512-d CLIP embeddings per photo | 🟠 semantic fingerprints |
| **512-d ArcFace embeddings per detected face** | 🔴 **biometric data** |
| Face bounding boxes + `det_score` | 🟠 |
| Person clusters, user-assigned **names, relations, ages, locations** | 🔴 An explicit social graph of the user's family, annotated by the user |
| Co-occurrence edges with counts and timestamps | 🔴 Who the user is with, and how often |
| `FaceCrops/*.jpg` — **an aligned face image for every detection** | 🔴 Thousands of face images in a single directory |

**The honest statement:** *"Vanta doesn't send biometric data anywhere — but it does **create** a concentrated,
structured biometric dataset that didn't exist before. The photos were always on the phone; a queryable index of
every face in them, clustered by identity and labelled 'Mom', was not. That's a real new risk surface I introduced,
and 'it never leaves the device' doesn't fully answer it."*

### 2.2 Are face embeddings "just numbers"?

No — treat this claim as false and be ready to say why:

1. **They are biometric data under GDPR Article 9** (and comparable regimes such as Illinois BIPA, which has
   produced substantial litigation). A "unique identifier derived from processing of biometric data for the purpose
   of uniquely identifying a natural person" is a special category regardless of its representation. It is legally
   irrelevant that it's a float array.
2. **They are linkable.** Two independent apps extracting ArcFace embeddings of the same person get *similar*
   vectors. The embedding is a stable cross-system identifier — which is exactly what makes it useful and exactly
   what makes it dangerous.
3. **Template-inversion attacks are a real, published research area.** Face-reconstruction-from-embedding work has
   repeatedly shown that recognisable face images can be reconstructed from embeddings, especially with access to
   the same model — and Vanta ships the model in its own APK.
4. **And Vanta doesn't even need inversion**, because it stores the actual aligned face crops as JPEGs on disk.

### 2.3 Backups

`android:allowBackup` is not set in the module manifest and would live in the generated app manifest (gitignored,
so unverifiable here). If it defaults to `true`, `vanta.db` and `FaceCrops/` can be swept into **Android Auto
Backup → Google Drive**, which would mean the face database leaves the device — silently contradicting the
product's central promise.

> **This is the single highest-priority security fix**, and it is one line:
> ```xml
> <application android:allowBackup="false" android:fullBackupContent="false" ... >
> ```
> or a `data_extraction_rules.xml` excluding the database and crops directory.

### 2.4 A command-injection hazard

```cpp
// vanta.cpp::resetFaceDataNative
std::string cmd = "rm -rf " + crops_dir + "/*";
system(cmd.c_str());
```

`crops_dir` comes from `VantaEngineConfig.getCropsDirectory(context)` → `File(context.filesDir, "FaceCrops")`, so
today it is fully app-controlled and cannot be influenced by a user or an attacker. **This is not currently
exploitable.**

But it is still the wrong construct, and worth flagging exactly that way:
- `system()` forks a shell and depends on `/system/bin/sh` and shell glob expansion — brittle across Android versions.
- `rm -rf <unvalidated string>/*` is one refactor away from a catastrophe. If `crops_dir` were ever empty, the
  command becomes `rm -rf /*`.
- Its return value is ignored, so a failure is invisible.

**The fix:** `std::filesystem::remove_all(crops_dir)` then recreate the directory, or iterate with
`std::filesystem::directory_iterator`. No shell, no globbing, no injection surface, and you get error codes.

> **Interview framing:** *"It's not exploitable today because the path is constructed entirely from
> `context.filesDir`. But building a shell command by string concatenation and running `rm -rf` on it is a pattern I
> shouldn't ship, because its safety depends on an invariant that nothing enforces. `std::filesystem::remove_all` is
> the same operation with none of the risk."*

### 2.5 Other surfaces

| Surface | Status |
|---|---|
| **SQL injection** | 🟢 **Mostly good.** Every user-supplied value goes through `sqlite3_bind_*`. The entity-id lists in `verify_entities_in_file` and `get_combined_membership_scores` are string-concatenated — but from `int64_t` values that came out of the database, so they cannot carry a payload. Still worth converting to bound parameters or a carray |
| **JSON injection** | 🟢 Handled — `json_utils.hpp` escapes strings, and the comment says this was a deliberate fix. Paths containing `"` or `\` won't corrupt the response |
| **Exported components** | 🟢 `IndexingService` is `android:exported="false"` ✅ |
| **Native memory safety** | 🔴 Two known UAF/race paths ([07](./07_CPP_CORE_ENGINE.md)). Memory-safety bugs in a library that parses attacker-influenced input (image files!) are a real attack surface — OpenCV/libjpeg decoding of a malicious image is a classic RCE vector |
| **Model integrity** | 🟠 Models are extracted from the APK (signed) into `filesDir` and loaded from there with no checksum. An attacker with write access to `filesDir` already owns the app, so the marginal risk is low |
| **Logging** | 🟠 `LOGI("Search complete…")`, `getIndexProgressNative` exposing `currentFile`, and `LOGI(" span: label='%s' text='%s'")` write **user queries and file paths to logcat**. On modern Android other apps can't read your logs, but they're in bug reports and visible over `adb`. Production builds should compile these out |
| **Screen capture / recents** | 🟠 No `FLAG_SECURE`; the People page with named faces appears in the recents thumbnail |
| **At-rest encryption** | 🔴 None. See below |

### 2.6 Deletion

| Requirement | Today |
|---|---|
| Delete one person | ❌ No UI. You can rename, not delete |
| Delete all face data | ✅ `resetFaceData` — but it **immediately re-runs the whole face pipeline**, so it's a *recompute*, not a *delete* |
| Delete the index | ⚠️ Only by uninstalling the app |
| Forget a specific photo | ❌ |
| Secure deletion | ❌ `DELETE` leaves data in free pages. `PRAGMA secure_delete = ON` would zero them |

### The security improvements, ranked

1. **`android:allowBackup="false"`** — one line, prevents the face DB reaching cloud backup. 🔴
2. **`PRAGMA secure_delete = ON`** — one line.
3. **Replace `system("rm -rf …")` with `std::filesystem`** — removes a whole class of risk.
4. **SQLCipher** (or Jetpack Security + a keystore-held key) to encrypt `vanta.db` at rest, with the key in the
   **Android Keystore** gated on device unlock (`setUnlockedDeviceRequired`). Cost: a measurable read/write
   overhead on every query.
5. **Stop storing face crops**; render avatars on demand from the bbox ([15](./15_SQLITE_AND_SQLITE_VEC.md)) — this
   removes thousands of face JPEGs *and* saves 4.5 GB.
6. **Real deletion UI**: delete a person, delete the whole index, exclude a folder from indexing.
7. **`FLAG_SECURE`** on the People and EntityDetail screens.
8. **Strip logging in release builds.**
9. **Fix the memory-safety bugs** — they're an attack surface, not just a stability issue.

---

## Part 3 — Fairness

### The problem is real and measurable

The reference work here is **NIST FRVT Part 3: Demographic Effects** (Grother, Ngan & Hanaoka, NISTIR 8280,
<https://pages.nist.gov/frvt/>), which evaluated ~190 algorithms and found **false-positive differentials across
demographic groups spanning one to two orders of magnitude** in many algorithms — with false positives generally
more variable across groups than false negatives, and the effect strongly dependent on the training data's
composition. Earlier work (Buolamwini & Gebru, *Gender Shades*, 2018) established the same pattern for face
*classification*.

### How this specifically affects Vanta

Vanta's exposure is **worse than a typical verification system**, for a structural reason worth stating clearly:

> "Most fairness discussion is about a verification threshold — does this person get through the gate. Vanta's
> thresholds feed **clustering**, which is a global partitioning decision. A higher false-match rate for a
> demographic group doesn't just cause an occasional error; it causes **systematically wrong identity merges** for
> that group. Concretely: a user whose family members are in a demographic where `w600k_r50` has elevated
> false-match rates will see their relatives merged into a single 'person' more often. The product is *measurably
> worse for them*, and there is nothing in the UI that tells them why."

| Factor | Effect on Vanta |
|---|---|
| **Training data composition** | `w600k_r50` was trained on WebFace600K. Its demographic composition determines the error profile, and Vanta inherits it wholesale |
| **A single global threshold** | `GOOD_THRESHOLD = 0.6` is one number for every user and every identity. If the optimal operating point differs by group, a fixed threshold *guarantees* unequal error rates |
| **Lighting / skin tone** | Under-exposure of darker skin tones in auto-exposed phone photos is a well-documented capture-side problem that compounds the model-side one |
| **Age variation** | Children's faces change fast. A family library spanning years will fragment children's identities more than adults' |
| **Pose** | Candid photos are far less frontal than benchmark data |
| **Domain shift** | Phone snapshots vs curated web faces |
| **INT8 quantisation** | 🔴 **Unmeasured, and it may not degrade uniformly.** Quantisation error can disproportionately affect groups already near the decision boundary. Nobody checked |

### What Apple's research contributes here

Apple's *"Recognizing People in Photos Through Private On-Device Machine Learning"*
(<https://machinelearning.apple.com/research/recognizing-people-photos>) is directly relevant because it addresses
the same deployment shape — on-device, private, unsupervised clustering over a personal library — and its design
choices are largely *robustness* choices: combining face **and upper-body** embeddings so that the system isn't
dependent on a clean frontal face, filtering out low-confidence/unclear faces rather than forcing a decision, and
using conservative clustering that tolerates leaving things unassigned. Every one of those makes the system less
brittle for the cases where a face model is weakest — which are disproportionately the cases where demographic
disparities appear. See [21](./21_APPLE_PHOTOS_COMPARISON.md).

### What production-level evaluation would require

> "I have not evaluated Vanta for demographic fairness, and I want to be clear that I also *can't* with what I have —
> I have no labelled data and the whole product premise forbids collecting any from users. So the honest answer is
> what a real evaluation would need:"

1. **A demographically annotated public benchmark.** RFW (Racial Faces in-the-Wild), BUPT-Balancedface, or the
   FRVT protocol. None involves user data.
2. **Report per-group, not aggregate.** FMR and FNMR **per demographic group at a fixed operating threshold** — and
   the ratio between the best and worst group, because that ratio is the fairness metric. An aggregate accuracy
   number actively hides the problem.
3. **Evaluate the clustering, not just the embedding.** Pairwise precision/recall, cluster purity and the number of
   identity merges **per group**, on simulated personal libraries. This is the part specific to Vanta and the part
   nobody usually measures.
4. **Measure the quantisation delta per group.** Run FP32 vs INT8 `w600k_r50` and check whether the degradation is
   uniform. This is cheap and, as far as I know, rarely reported.
5. **Adaptive thresholds.** Replace the global constant with per-identity calibration from the library's own
   intra-class and (co-occurrence-derived) impostor distributions — see
   [11](./11_FACE_CLUSTERING_AND_PERSON_SEGREGATION.md). This is the mitigation that doesn't require any
   demographic data at all, which makes it the right one for a privacy-first product.
6. **Design for graceful uncertainty.** The deepest mitigation is product-shaped: make "I'm not sure" a first-class
   outcome. A system that leaves ambiguous faces unassigned and asks the user degrades *fairly*, because it fails
   the same way for everyone — with a question rather than a wrong answer.

> **The closing line:** *"The fairness risk here isn't hypothetical or abstract — it's that my `GOOD_THRESHOLD`
> is a single hardcoded float that I chose by trying a few values on my own photo library. That's a calibration
> procedure with a sample size of one family. Adaptive, per-identity thresholds calibrated from each user's own data
> are both the fairer design and, conveniently, the more private one."*
