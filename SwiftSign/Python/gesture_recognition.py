import time
from collections import deque, Counter

import cv2
import mediapipe as mp
import requests

from config import (
    CAMERA_STREAM_URL,
    ESP32_CONTROLLER_URL,
    MIN_CONFIDENCE,
    DEBUG_MODE
)

from phrase_mapper import PhraseMapper


mp_hands = mp.solutions.hands
mp_drawing = mp.solutions.drawing_utils

phrase_mapper = PhraseMapper()

PREDICTION_BUFFER_SIZE = 15
CONFIRMATION_REQUIRED = 11

prediction_buffer = deque(maxlen=PREDICTION_BUFFER_SIZE)
phrase_locked = False


def finger_is_up(landmarks, tip_id, pip_id):
    return landmarks[tip_id].y < landmarks[pip_id].y


def get_finger_states(hand_landmarks):
    lm = hand_landmarks.landmark

    index = finger_is_up(lm, 8, 6)
    middle = finger_is_up(lm, 12, 10)
    ring = finger_is_up(lm, 16, 14)
    pinky = finger_is_up(lm, 20, 18)

    return index, middle, ring, pinky


def thumb_up(hand_landmarks):
    lm = hand_landmarks.landmark

    thumb_tip = lm[4]
    thumb_ip = lm[3]
    wrist = lm[0]

    return (
        thumb_tip.y < thumb_ip.y and
        thumb_tip.y < wrist.y - 0.08
    )


def thumb_down(hand_landmarks):
    lm = hand_landmarks.landmark

    thumb_tip = lm[4]
    thumb_ip = lm[3]
    wrist = lm[0]

    return (
        thumb_tip.y > thumb_ip.y and
        thumb_tip.y > wrist.y + 0.08
    )


def thumb_extended_side(hand_landmarks):
    lm = hand_landmarks.landmark
    return abs(lm[4].x - lm[2].x) > 0.06


def fingers_spread(hand_landmarks):
    lm = hand_landmarks.landmark

    gap_index_middle = abs(lm[8].x - lm[12].x)
    gap_middle_ring = abs(lm[12].x - lm[16].x)
    gap_ring_pinky = abs(lm[16].x - lm[20].x)

    total_gap = gap_index_middle + gap_middle_ring + gap_ring_pinky

    return total_gap > 0.18


def classify_single_hand(hand_landmarks):
    index, middle, ring, pinky = get_finger_states(hand_landmarks)
    count = sum([index, middle, ring, pinky])

    thumb_is_up = thumb_up(hand_landmarks)
    thumb_is_down = thumb_down(hand_landmarks)
    thumb_side = thumb_extended_side(hand_landmarks)

    # Yes: thumb up and other fingers folded
    if thumb_is_up and count == 0:
        return "YES", 0.95

    # No: thumb down and other fingers folded
    if thumb_is_down and count == 0:
        return "NO", 0.95

    # Reset: closed fist
    if count == 0 and not thumb_is_up and not thumb_is_down:
        return "RESET", 1.0

    # Hello: four fingers open and spread
    if count == 4 and fingers_spread(hand_landmarks):
        return "HELLO", 0.95

    # Goodbye: four fingers open and close together
    if count == 4 and not fingers_spread(hand_landmarks):
        return "GOODBYE", 0.95

    # Question: index only
    if index and not middle and not ring and not pinky:
        return "QUESTION", 0.92

    # Help: index + middle
    if index and middle and not ring and not pinky:
        return "HELP", 0.92

    # Restroom: index + middle + ring
    if index and middle and ring and not pinky:
        return "RESTROOM", 0.92

    # Thank you: thumb + pinky
    if thumb_side and pinky and not index and not middle and not ring:
        return "THANK_YOU", 0.92

    return "UNKNOWN", 0.0


def classify_gesture(results):
    if not results.multi_hand_landmarks:
        return "NO_HAND", 0.0

    hand_landmarks = results.multi_hand_landmarks[0]
    return classify_single_hand(hand_landmarks)


def get_confirmed_prediction(gesture_key):
    if gesture_key in ["UNKNOWN", "NO_HAND"]:
        prediction_buffer.clear()
        return None

    prediction_buffer.append(gesture_key)

    if len(prediction_buffer) < PREDICTION_BUFFER_SIZE:
        return None

    counts = Counter(prediction_buffer)
    most_common_gesture, amount = counts.most_common(1)[0]

    if most_common_gesture == gesture_key and amount >= CONFIRMATION_REQUIRED:
        return most_common_gesture

    return None


def should_send(gesture_key):
    global phrase_locked

    if gesture_key == "RESET":
        phrase_locked = False
        return False

    if phrase_locked:
        return False

    phrase_locked = True
    return True


def send_to_esp32(phrase_data, confidence):
    try:
        url = f"{ESP32_CONTROLLER_URL}/gesture"

        params = {
            "gesture": phrase_data["gesture"],
            "text": phrase_data["display"],
            "track": phrase_data["audio_track"],
            "confidence": confidence
        }

        response = requests.get(url, params=params, timeout=2)

        if DEBUG_MODE:
            print("Sent:", params)
            print("ESP32 response:", response.text)

    except Exception as error:
        print("Failed to send to ESP32:", error)


def main():
    cap = cv2.VideoCapture(CAMERA_STREAM_URL)

    if not cap.isOpened():
        print("Could not open ESP32-CAM stream.")
        print("Check CAMERA_STREAM_URL in config.py.")
        return

    with mp_hands.Hands(
        static_image_mode=False,
        max_num_hands=1,
        min_detection_confidence=0.6,
        min_tracking_confidence=0.6
    ) as hands:

        while True:
            success, frame = cap.read()

            if not success:
                print("Failed to read frame.")
                time.sleep(0.3)
                continue

            frame = cv2.flip(frame, 1)
            rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            results = hands.process(rgb_frame)

            raw_gesture = "NO_HAND"
            confirmed_gesture = None
            confidence = 0.0
            display_text = "No hand detected"

            if results.multi_hand_landmarks:
                for hand_landmarks in results.multi_hand_landmarks:
                    mp_drawing.draw_landmarks(
                        frame,
                        hand_landmarks,
                        mp_hands.HAND_CONNECTIONS
                    )

                raw_gesture, confidence = classify_gesture(results)
                confirmed_gesture = get_confirmed_prediction(raw_gesture)

                if confirmed_gesture:
                    if confirmed_gesture == "RESET":
                        should_send("RESET")
                        display_text = "Ready"
                    else:
                        phrase_data = phrase_mapper.get_phrase(confirmed_gesture)
                        display_text = phrase_data["display"]

                        if confidence >= MIN_CONFIDENCE and should_send(confirmed_gesture):
                            send_to_esp32(phrase_data, confidence)

                elif raw_gesture == "RESET":
                    display_text = "Ready"
                elif raw_gesture != "UNKNOWN":
                    display_text = "Hold steady..."
                else:
                    display_text = "Unknown gesture"

            else:
                prediction_buffer.clear()

            cv2.putText(frame, f"Raw: {raw_gesture}", (20, 40),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)

            cv2.putText(frame, f"Confirmed: {confirmed_gesture if confirmed_gesture else '-'}", (20, 80),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)

            cv2.putText(frame, f"Phrase: {display_text}", (20, 120),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

            cv2.putText(frame, f"Confidence: {confidence:.2f}", (20, 160),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

            cv2.imshow("SwiftSign Gesture Recognition", frame)

            if cv2.waitKey(1) & 0xFF == ord("q"):
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()