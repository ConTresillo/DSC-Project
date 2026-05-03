#include <WiFi.h>
#include <WebServer.h>

#define NUM_SLOTS 2

// ================= WIFI =================
const char* ssid = "PotsHot";
const char* password = "1234"; 

WebServer server(80);

// ================= TIMING =================
const unsigned long RESERVATION_WINDOW_MS   = 20000;
const unsigned long VERIFICATION_WINDOW_MS  = 10000;
const unsigned long INVALID_WINDOW_MS       = 10000;
const unsigned long BLINK_INTERVAL_MS       = 500;

// ================= PINS =================
int irPins[NUM_SLOTS] = {35, 34};

int actionPins[NUM_SLOTS]  = {26, 33};
int declinePins[NUM_SLOTS] = {27, 32};

int redPins[NUM_SLOTS]   = {4, 19};
int greenPins[NUM_SLOTS] = {5, 23};
int bluePins[NUM_SLOTS]  = {18, 25};

int buzzerPins[NUM_SLOTS] = {22, 21};

// ================= FSM =================
enum SlotState {
  FREE,
  RESERVED,
  VERIFICATION_PENDING,
  OCCUPIED_VALID,
  OCCUPIED_INVALID,
  ESCALATED
};

SlotState state[NUM_SLOTS];

// ================= DATA =================
bool occupied[NUM_SLOTS];

unsigned long reservationDeadline[NUM_SLOTS];
unsigned long verificationDeadline[NUM_SLOTS];
unsigned long invalidDeadline[NUM_SLOTS];
unsigned long sessionStartTime[NUM_SLOTS];
unsigned long escalatedStartTime[NUM_SLOTS];

// ================= OWNERSHIP =================
String ownerIP[NUM_SLOTS] = {"", ""};

// ================= INPUT =================
bool prevAction[NUM_SLOTS]  = {HIGH};
bool prevDecline[NUM_SLOTS] = {HIGH};

bool currAction[NUM_SLOTS];
bool currDecline[NUM_SLOTS];

const unsigned long DEBOUNCE_MS = 50;

// ✅ 3 BUFFERS
bool bookBuffer[NUM_SLOTS]    = {false};
bool verifyBuffer[NUM_SLOTS]  = {false};
bool declineBuffer[NUM_SLOTS] = {false};

unsigned long lastActionTime[NUM_SLOTS]  = {0};
unsigned long lastDeclineTime[NUM_SLOTS] = {0};

bool fallingEdge(bool p, bool c) {
  return (p == HIGH && c == LOW);
}

// ================= SENSOR =================
bool isSlotOccupied(int i) {
  return digitalRead(irPins[i]) == LOW;
}

// ================= STATE TRANSITIONS =================
void enterFree(int i) {
  state[i] = FREE;
  ownerIP[i] = "";   // ✅ ADDED
  Serial.printf("Slot %d -> FREE\n", i+1);
}

void enterReserved(int i) {
  state[i] = RESERVED;
  Serial.printf("Slot %d -> RESERVED\n", i+1);
}

void enterVerification(int i, unsigned long now) {
  state[i] = VERIFICATION_PENDING;
  verificationDeadline[i] = now + VERIFICATION_WINDOW_MS;
  Serial.printf("Slot %d -> VERIFY\n", i+1);
}

void enterValid(int i, unsigned long now) {
  state[i] = OCCUPIED_VALID;
  sessionStartTime[i] = now;
  Serial.printf("Slot %d -> VALID\n", i+1);
}

void enterInvalid(int i, unsigned long now) {
  state[i] = OCCUPIED_INVALID;
  invalidDeadline[i] = now + INVALID_WINDOW_MS;
  Serial.printf("Slot %d -> INVALID\n", i+1);
}

void enterEscalated(int i, unsigned long now) {
  state[i] = ESCALATED;
  escalatedStartTime[i] = now;
  Serial.printf("Slot %d -> ESCALATED\n", i+1);
}

// ================= OUTPUT =================
void setRGB(int i, bool r, bool g, bool b) {
  digitalWrite(redPins[i], r);
  digitalWrite(greenPins[i], g);
  digitalWrite(bluePins[i], b);
}

void updateOutputs(int i, unsigned long now) {

  bool blink = (now / BLINK_INTERVAL_MS) % 2;

  switch (state[i]) {

    case RESERVED:
      setRGB(i, HIGH, HIGH, LOW);
      digitalWrite(buzzerPins[i], LOW);
      break;

    case VERIFICATION_PENDING:
      setRGB(i, blink, blink, LOW);
      digitalWrite(buzzerPins[i], LOW);
      break;

    case OCCUPIED_VALID:
      if (now - sessionStartTime[i] < 1000)
        setRGB(i, LOW, HIGH, LOW);
      else
        setRGB(i, LOW, LOW, LOW);
      break;

    case OCCUPIED_INVALID:
      setRGB(i, HIGH, LOW, LOW);
      digitalWrite(buzzerPins[i], blink);
      break;

    case ESCALATED:
      setRGB(i, blink, LOW, LOW);
      digitalWrite(buzzerPins[i], blink);
      break;

    default:
      setRGB(i, LOW, LOW, LOW);
      digitalWrite(buzzerPins[i], LOW);
      break;
  }
}

// ================= INPUT PROCESSING =================
void processInputs(int i, unsigned long now) {

  if (fallingEdge(prevAction[i], currAction[i]) &&
      (now - lastActionTime[i] > DEBOUNCE_MS)) {

    if (state[i] == FREE)
      bookBuffer[i] = true;
    else
      verifyBuffer[i] = true;

    lastActionTime[i] = now;
  }

  if (fallingEdge(prevDecline[i], currDecline[i]) &&
      (now - lastDeclineTime[i] > DEBOUNCE_MS)) {

    declineBuffer[i] = true;
    lastDeclineTime[i] = now;
  }
}

// ================= FSM =================
void updateFSM(int i, unsigned long now) {

  // BOOK
  if (bookBuffer[i]) {
    if (state[i] == FREE) {
      reservationDeadline[i] = now + RESERVATION_WINDOW_MS; // ✅ only here
      enterReserved(i);
    }
    bookBuffer[i] = false;
  }

  // VERIFY
  if (verifyBuffer[i]) {

    if (state[i] == VERIFICATION_PENDING ||
        state[i] == OCCUPIED_INVALID) {
      enterValid(i, now);
    }

    else if (state[i] == ESCALATED) {

      unsigned long paused = now - escalatedStartTime[i];
      reservationDeadline[i]  += paused;
      verificationDeadline[i] += paused;
      invalidDeadline[i]      += paused;

      enterValid(i, now);
    }

    verifyBuffer[i] = false;
  }

  // DECLINE
  if (declineBuffer[i]) {

    if (state[i] == VERIFICATION_PENDING)
      enterInvalid(i, now);

    else if (state[i] == ESCALATED) {

      unsigned long paused = now - escalatedStartTime[i];
      reservationDeadline[i]  += paused;
      verificationDeadline[i] += paused;
      invalidDeadline[i]      += paused;

      state[i] = RESERVED;
    }

    declineBuffer[i] = false;
  }

  // ORIGINAL FSM (unchanged)
  switch (state[i]) {

    case FREE:
      if (occupied[i]) enterValid(i, now);
      break;

    case RESERVED:
      if (now >= reservationDeadline[i]) enterFree(i);
      else if (occupied[i]) enterVerification(i, now);
      break;

    case VERIFICATION_PENDING:
      if (now >= reservationDeadline[i]) enterInvalid(i, now);   // 🔥 priority
      else if (now >= verificationDeadline[i]) enterInvalid(i, now);
      else if (!occupied[i]) enterReserved(i);
      break;

    case OCCUPIED_VALID:
      if (!occupied[i]) enterFree(i);
      break;

    case OCCUPIED_INVALID:
      if (now >= reservationDeadline[i]) enterEscalated(i, now);   // 🔥 priority
      else if (now >= invalidDeadline[i]) enterEscalated(i, now);
      else if (!occupied[i]) enterReserved(i);
      break;

    case ESCALATED:
      break;
  }
}

bool isOwner(int i) {
  if (i < 0 || i >= NUM_SLOTS) return false;
  String ip = server.client().remoteIP().toString();
  return ip == ownerIP[i];
}

String stateToString(SlotState s) {
  switch (s) {
    case FREE: return "FREE";
    case RESERVED: return "RESERVED";
    case VERIFICATION_PENDING: return "VERIFY";
    case OCCUPIED_VALID: return "VALID";
    case OCCUPIED_INVALID: return "INVALID";
    case ESCALATED: return "ESCALATED";
    default: return "UNKNOWN";
  }
}

// ================= HTTP =================
void handleBook() {
  if (!server.hasArg("slot")) {
    server.send(400, "text/plain", "Missing slot");
    return;
  }
  int i = server.arg("slot").toInt();
  if (i < 0 || i >= NUM_SLOTS) {
    server.send(400, "text/plain", "Invalid slot");
    return;
  }

  if (state[i] != FREE) {
    server.send(409, "text/plain", "Slot not free");
    return;
  }

  // ✅ ADDED: assign owner
  ownerIP[i] = server.client().remoteIP().toString();

  Serial.print("Slot ");
  Serial.print(i);
  Serial.print(" booked by ");
  Serial.println(ownerIP[i]);

  bookBuffer[i] = true;
  server.send(200, "text/plain", "Book triggered");
}

void handleVerify() {
  if (!server.hasArg("slot")) {
    server.send(400, "text/plain", "Missing slot");
    return;
  }
  int i = server.arg("slot").toInt();
  if (i < 0 || i >= NUM_SLOTS) {
    server.send(400, "text/plain", "Invalid slot");
    return;
  }

  // ✅ ADDED
  if (!isOwner(i)) {
    server.send(403, "text/plain", "Not owner");
    return;
  }

  if (!(state[i] == VERIFICATION_PENDING ||
        state[i] == OCCUPIED_INVALID ||
        state[i] == ESCALATED)) {

    server.send(409, "text/plain", "Verify not allowed in current state");
    return;
  }

  verifyBuffer[i] = true;
  server.send(200, "text/plain", "Verify triggered");
}

void handleDecline() {
  if (!server.hasArg("slot")) {
    server.send(400, "text/plain", "Missing slot");
    return;
  }
  int i = server.arg("slot").toInt();
  if (i < 0 || i >= NUM_SLOTS) {
    server.send(400, "text/plain", "Invalid slot");
    return;
  }

  // ✅ ADDED
  if (!isOwner(i)) {
    server.send(403, "text/plain", "Not owner");
    return;
  }

  if (!(state[i] == VERIFICATION_PENDING ||
        state[i] == ESCALATED)) {

    server.send(409, "text/plain", "Decline not allowed in current state");
    return;
  }


  declineBuffer[i] = true;
  server.send(200, "text/plain", "Decline triggered");
}

void handleStatus() {
  String ip = server.client().remoteIP().toString();
  for (int i = 0; i < NUM_SLOTS; i++) {

    if (ownerIP[i] == ip &&
        state[i] == VERIFICATION_PENDING) {

      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/plain", "VERIFY:" + String(i));
      return;
    }
  }
}

void handleState() {

  String json = "{ \"slots\": [";

  for (int i = 0; i < NUM_SLOTS; i++) {
    json += "{";
    json += "\"id\":" + String(i) + ",";
    json += "\"state\":\"" + stateToString(state[i]) + "\"";
    json += "}";

    if (i < NUM_SLOTS - 1) json += ",";
  }

  json += "] }";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());

  server.on("/book", handleBook);
  server.on("/verify", handleVerify);
  server.on("/decline", handleDecline);
  server.on("/status", handleStatus);
  server.on("/state", handleState);
  server.begin();

  for (int i = 0; i < NUM_SLOTS; i++) {

    pinMode(irPins[i], INPUT);
    pinMode(actionPins[i], INPUT_PULLUP);
    pinMode(declinePins[i], INPUT_PULLUP);

    pinMode(redPins[i], OUTPUT);
    pinMode(greenPins[i], OUTPUT);
    pinMode(bluePins[i], OUTPUT);
    pinMode(buzzerPins[i], OUTPUT);

    state[i] = FREE;
  }
}

// ================= LOOP =================
void loop() {

  server.handleClient();

  unsigned long now = millis();

  for (int i = 0; i < NUM_SLOTS; i++) {

    occupied[i] = isSlotOccupied(i);

    currAction[i]  = digitalRead(actionPins[i]);
    currDecline[i] = digitalRead(declinePins[i]);

    processInputs(i, now);
    updateFSM(i, now);
    updateOutputs(i, now);

    prevAction[i]  = currAction[i];
    prevDecline[i] = currDecline[i];
  }
}