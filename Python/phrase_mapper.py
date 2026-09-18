import json
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent
PHRASE_DB_PATH = BASE_DIR / "vocabulary" / "phrase_database.json"


class PhraseMapper:
    def __init__(self):
        self.phrases = self._load_phrases()

    def _load_phrases(self):
        if not PHRASE_DB_PATH.exists():
            raise FileNotFoundError(f"Missing file: {PHRASE_DB_PATH}")

        with open(PHRASE_DB_PATH, "r", encoding="utf-8") as file:
            return json.load(file)

    def get_phrase(self, gesture_key):
        gesture_key = gesture_key.upper()

        if gesture_key not in self.phrases:
            return {
                "gesture": gesture_key,
                "display": "Unknown gesture",
                "audio_track": 0,
                "category": "Unknown"
            }

        data = self.phrases[gesture_key]

        return {
            "gesture": gesture_key,
            "display": data["display"],
            "audio_track": data["audio_track"],
            "category": data["category"]
        }


if __name__ == "__main__":
    mapper = PhraseMapper()

    for gesture in [
        "HELLO",
        "GOODBYE",
        "YES",
        "NO",
        "QUESTION",
        "HELP",
        "RESTROOM",
        "THANK_YOU"
    ]:
        print(mapper.get_phrase(gesture))