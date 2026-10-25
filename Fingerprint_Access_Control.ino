// tell them to be cleaning the sensor
#include "biometrics.h"
#include "buttons.h"
#include "users.h"
#include "myTime.h"
#include "logger.h"
#include "audio.h"
#include "debugMacro.h"
#include "door.h"

typedef enum {
  MENU,
  ACCESS,
  ENROLL,
  DELETE,
  LOG,
  SET_TIME,
  PASSAGE
} menu_state;

#define SPEAKER_PIN PA15
#define LATE_HOUR 22  // hour to automatically lock door if left open


const char *EnrollModeHeader = "Enroll Mode";
const char *AccessModeHeader = "Access Mode";
const char *DeleteModeHeader = "Delete Mode";
const char *LogModeHeader = "Log Mode";
const char *PassageModeHeader = "DOOR OPEN";

menu_state mode = ACCESS;
//char *mode = ACCESS;   // start with access control mode
uint8_t menuPage = 1;    // main menu page (1 or 2)
bool doorLocked = true;  // door is locked initially.
bool UsersExists = false;

bool exit();
void lockDoor();
void unlockDoor();
bool verifyMasterPermission(const char *modeHeader);

void setup() {
  Serial.begin(115200);
  delay(3000);
  biometricsSetup();
  audioInit(SPEAKER_PIN);
  audioStartup();
  buttonConfig();
  loggerInit();
  while (!checkDisplay())
    ;
  delay(1000);
  debugln("starting");

  if (!timeConfig()) {
    display_three_words("time", "not", "set", 2);
    delay(2000);
  }
  pinMode(DOOR_PIN, OUTPUT);
  lockDoor();


  enrolledUsers = getEnrolledCount();
  if (enrolledUsers == 0) {
    mode = ENROLL;
    debugln("No users found. Entering enroll mode.");
  } else {
    UsersExists = true;
    debugf("found %i user(s)\n", enrolledUsers);
  }

  debugln(getTime());
  debugln(getDate());
}

void loop() {
  //checked
  if (mode == MENU) {
    debugf("in menu (page %d)\n", menuPage);
    displayMenu(menuPage);
    delay(300);
    while (mode == MENU) {
      if (menuPage == 1) {
        if (RIGHT_BUTTON.pressed()) {
          audioBeep();
          menuPage = 2;
          debugln("Switched to Menu Page 2");
          break;
        } else if (UP_BUTTON.pressed()) {
          audioBeep();
          mode = ENROLL;
          debugln("Selected: enroll");
          break;
        } else if (DOWN_BUTTON.pressed()) {
          audioBeep();
          mode = ACCESS;
          debugln("Selected: access");
          break;
        } else if (LEFT_BUTTON.pressed()) {
          audioBeep();
          mode = DELETE;
          debugln("Selected: delete");
          break;
        }
      } else {  // menuPage == 2
        if (RIGHT_BUTTON.pressed()) {
          audioBeep();
          menuPage = 1;
          debugln("Switched back to Menu Page 1");
          break;
        } else if (UP_BUTTON.pressed()) {
          audioBeep();
          mode = LOG;
          debugln("Selected: log");
          break;
        } else if (DOWN_BUTTON.pressed()) {
          audioBeep();
          mode = SET_TIME;
          debugln("Selected: settime");
          break;
        }
      }
    }
  }
  //checked
  /////////////////////////////////////// Enroll Mode //////////////////////////////////
  if (mode == ENROLL) {
    if (UsersExists && !verifyMasterPermission(EnrollModeHeader)) {
      mode = MENU;
    }

    while (mode == ENROLL) {
      if (exit()) { break; }
      UserRecord user;
      uint16_t current_id = getNextAvailableID();
      if (current_id == 0 || current_id > MAX_USERS) {
        debugln("no space for new enrollment");
        audioWarning();
        displayTwoWords("no", "space", EnrollModeHeader);
        mode = MENU;
        delay(1500);
        break;
      }

      debugln("Ready to enroll a fingerprint!");
      user.id = current_id;

      if (!UsersExists) {
        user.notMaster = 0;  // First user is Master
        debugln("First user initialized as Master");
        getUserName(user.name);
        user.name[sizeof(user.name) - 1] = '\0';
      } else {
        getUserName(user.name);
        user.name[sizeof(user.name) - 1] = '\0';

        // Ask if new user should be made a master
        while (mode == ENROLL) {
          displayOption("Master??");
          char *option = choice();
          if (strcmp(option, "yes") == 0) {
            user.notMaster = 0;
            debugf("%s is set as master\n", user.name);
            displayTwoWords("Master", user.name, EnrollModeHeader);
            delay(1000);
            break;
          } else if (strcmp(option, "no") == 0) {
            user.notMaster = 1;
            displayTwoWords("Not", "Master", EnrollModeHeader);
            delay(1000);
            break;
          }
        }
      }

      // Enroll new user's fingerprint on AS608
      while (mode == ENROLL) {
        if (exit()) { break; }
        uint8_t success = getFingerprintEnroll(current_id);
        debugf("enrollment result %i\n", success);
        if (success == FINGERPRINT_OK) {
          saveUser(user);
          enrolledUsers++;
          UsersExists = true;
          debugln("saved user to EEPROM");
          break;
        }
        debugln("failed to enroll");
        delay(1500);
      }

      while (mode == ENROLL) {
        if (exit()) { break; }
        displayOption("Again??");
        char *option = choice();
        if (strcmp(option, "yes") == 0) {
          break;
        } else if (strcmp(option, "no") == 0) {
          mode = ACCESS;
          break;
        }
      }
    }
  }
  //checked
  /////////////////////////////////////// Access Mode //////////////////////////////////
  if (mode == ACCESS) {
    while (mode == ACCESS) {
      if (exit()) { break; }
      displayTwoWords("Place", "Finger", AccessModeHeader);
      SensorRsp rsp = getFingerprintID();
      if (!rsp.isError) {
        debugf("user with id %s unlocked the door\n", rsp.msg);
        char *endptr;
        uint16_t id = (uint16_t)strtol(rsp.msg, &endptr, 10);
        UserRecord user = getUser(id);
        audioAccessGranted();
        displayTwoWords("Welcome", user.name, AccessModeHeader);
        unlockDoor();
        if (user.notMaster == 0) {
          long currentMills = millis();
          while (millis() - currentMills < 5000) {
            char time_buff[11];
            snprintf(time_buff, sizeof(time_buff), "till %iPM?", LATE_HOUR - 12);
            displayOption(time_buff, "<NO  ", "YES> ");
            char* currentChoice = choice();
            if (currentChoice == "yes") {
              // set alarm
              setAlarm(LATE_HOUR, 0);
              char buff[5];
              snprintf(buff, sizeof(buff), "%iPM", LATE_HOUR - 12);
              display_three_words("open", "till", buff, 2);
              debugf("Alarm set for %iPM\n", LATE_HOUR-12);
              //enter passage mode: door stays unlocked until LATE_HOUR
              delay(1000);
              mode = PASSAGE;
              break;
            } else if (currentChoice == "no") {
              debugln("user selected no, locking door");
              display_three_words("locking", "in four", "secs", 2);
                             delay(4000);
              lockDoor();
              break;
            }
          }
        } else {
          delay(4000);
          lockDoor();
        }
        // if an alarm has not been set, then lock the door

        displayWord("Logging", AccessModeHeader);
        logAccess(user.name, true);  // log only successful entry
      } else {
        debugf("sensor err: %s\n", rsp.msg);
        if (strcmp(rsp.msg, "no match") == 0) {
          audioAccessDenied();
          displayWord("Invalid", AccessModeHeader);
          delay(1000);
        }
      }
    }
  }
  //checked
  /////////////////////////////////////// Delete Mode //////////////////////////////////
  if (mode == DELETE) {
    if (!verifyMasterPermission(DeleteModeHeader)) {
      mode = MENU;
    }

    while (mode == DELETE) {
      if (exit()) { break; }
      displayCustomMenu("Query Opt", "U-Scroll", "D-Name");

      if (UP_BUTTON.pressed()) {
        audioBeep();
        debugln("Scroll mode selected");
        if (enrolledUsers == 0) {
          audioWarning();
          displayTwoWords("No", "Users", DeleteModeHeader);
          delay(1500);
          continue;
        }

        uint16_t activeIDs[MAX_USERS];
        int activeCount = 0;
        for (uint16_t i = 1; i <= MAX_USERS; i++) {
          UserRecord u;
          if (userExist(i, u)) {
            activeIDs[activeCount++] = i;
          }
        }

        int currentIndex = 0;
        while (mode == DELETE) {
          if (exit()) { break; }
          if (activeCount == 0) {
            displayTwoWords("No", "Users", DeleteModeHeader);
            delay(1500);
            break;
          }

          UserRecord user = getUser(activeIDs[currentIndex]);
          char buffer[12];
          snprintf(buffer, sizeof(buffer), "Del %s", user.name);
          displayOption(buffer, "exit>", "<DEL");

          if (UP_BUTTON.pressed()) {
            audioBeep();
            currentIndex = (currentIndex + 1) % activeCount;
          } else if (DOWN_BUTTON.pressed()) {
            audioBeep();
            currentIndex = (currentIndex - 1 + activeCount) % activeCount;
          } else if (LEFT_BUTTON.pressed()) {
            audioBeep();
            if (user.notMaster == 0 && getMasterCount() <= 1) {
              debugln("Blocked deletion of sole Master user.");
              audioWarning();
              displayTwoWords("Can't", "Delete", DeleteModeHeader);
              delay(1500);
              continue;
            }
            deleteFingerprint(user.id);
            deleteUser(user);
            audioDelete();
            displayTwoWords(user.name, "deltd", DeleteModeHeader);
            delay(1500);

            // refresh active IDs
            activeCount = 0;
            for (uint16_t i = 1; i <= MAX_USERS; i++) {
              UserRecord checkU;
              if (userExist(i, checkU)) {
                activeIDs[activeCount++] = i;
              }
            }
            if (currentIndex >= activeCount) {
              currentIndex = activeCount > 0 ? activeCount - 1 : 0;
            }
          }
        }
      } else if (DOWN_BUTTON.pressed()) {
        audioBeep();
        debugln("Name search delete selected");
        char targetName[5] = { '@', '@', '@', '@', '\0' };
        getUserName(targetName);
        targetName[4] = '\0';

        int deletedCount = 0;
        for (uint16_t i = 1; i <= MAX_USERS; i++) {
          UserRecord u;
          if (userExist(i, u)) {
            if (strncmp(u.name, targetName, 4) == 0) {
              if (u.notMaster == 0 && getMasterCount() <= 1) {
                debugln("Blocked deletion of sole Master user during name search.");
                audioWarning();
                displayTwoWords("Can't", "Delete", DeleteModeHeader);
                delay(1500);
                continue;
              }
              deleteFingerprint(u.id);
              deleteUser(u);
              deletedCount++;
            }
          }
        }

        if (deletedCount > 0) {
          audioDelete();
          displayTwoWords(targetName, "deltd", DeleteModeHeader);
          delay(1500);
        } else {
          audioAccessDenied();
          displayTwoWords("Not", "Found", DeleteModeHeader);
          delay(1500);
        }
      }
    }
  }
  //checked
  /////////////////////////////////////// Log Mode //////////////////////////////////
  if (mode == LOG) {
    if (!verifyMasterPermission(LogModeHeader)) {
      mode = MENU;
    }

    while (mode == LOG) {
      if (exit()) { break; }
      int totalLogs = getLogCount();
      if (totalLogs == 0) {
        audioWarning();
        displayTwoWords("No", "Logs", LogModeHeader);
        delay(1500);
        mode = MENU;
        break;
      }

      int currentLogIdx = 0;
      while (mode == LOG) {
        if (exit()) { break; }
        LogRecord rec = getLog(currentLogIdx);
        displayLogEntry(rec.name, rec.date, rec.time, rec.doorState, currentLogIdx + 1, totalLogs);

        if (UP_BUTTON.pressed()) {
          audioBeep();
          currentLogIdx = (currentLogIdx + 1) % totalLogs;
        } else if (DOWN_BUTTON.pressed()) {
          audioBeep();
          currentLogIdx = (currentLogIdx - 1 + totalLogs) % totalLogs;
        }
      }
    }
  }

  /////////////////////////////////////// Set Time Mode //////////////////////////////////
  if (mode == SET_TIME) {
    if (!verifyMasterPermission("Set Time")) {
      mode = MENU;
    }

    uint8_t hours = 12;
    uint8_t minutes = 0;
    uint8_t day = 1;
    uint8_t month = 1;
    uint8_t year = 26;

    // Parse current time & date
    int hVal = 0, mVal = 0, dVal = 1, monVal = 1, yVal = 26;
    if (sscanf(getTime(), "%d:%d", &hVal, &mVal) == 2) {
      hours = (uint8_t)hVal;
      minutes = (uint8_t)mVal;
    }
    if (sscanf(getDate(), "%d/%d/%d", &dVal, &monVal, &yVal) == 3) {
      day = (uint8_t)dVal;
      month = (uint8_t)monVal;
      year = (uint8_t)yVal;
    }

    uint8_t fieldPos = 0;  // 0=Hours, 1=Minutes, 2=Day, 3=Month, 4=Year

    while (mode == SET_TIME) {
      displayTimeInput(day, month, year, hours, minutes, fieldPos);

      if (RIGHT_BUTTON.pressed()) {
        audioBeep();
        fieldPos = (fieldPos + 1) % 5;
        debugf("Time input active field: %d\n", fieldPos);
      }

      if (UP_BUTTON.pressed()) {
        audioBeep();
        if (fieldPos == 0) hours = (hours + 1) % 24;
        else if (fieldPos == 1) minutes = (minutes + 1) % 60;
        else if (fieldPos == 2) day = (day % 31) + 1;
        else if (fieldPos == 3) month = (month % 12) + 1;
        else if (fieldPos == 4) year = (year + 1) % 100;
      } else if (DOWN_BUTTON.pressed()) {
        audioBeep();
        if (fieldPos == 0) hours = (hours == 0) ? 23 : hours - 1;
        else if (fieldPos == 1) minutes = (minutes == 0) ? 59 : minutes - 1;
        else if (fieldPos == 2) day = (day <= 1) ? 31 : day - 1;
        else if (fieldPos == 3) month = (month <= 1) ? 12 : month - 1;
        else if (fieldPos == 4) year = (year == 0) ? 99 : year - 1;
      }

      if (LEFT_BUTTON.pressed()) {
        audioAccessGranted();
        setManualDateTime(day, month, year, hours, minutes);
        displayTwoWords("Time", "Set", "Set Time");
        delay(1500);
        mode = MENU;
        break;
      }
    }
  }

  if (mode == PASSAGE) {
    while (mode == PASSAGE) {
      displayTwoWords("Scan to", "lock", PassageModeHeader);
      //check aceess if true unlock
      SensorRsp rsp = getFingerprintID();
      if (!rsp.isError) {
        char *endptr;
        uint16_t id = (uint16_t)strtol(rsp.msg, &endptr, 10);
        UserRecord user = getUser(id);
        audioAccessGranted();
        lockDoor();
        logAccess(user.name, false);  // log who locked the door. false means the user locked the door
        disableAlarm();               // disable alarm
        mode = ACCESS;
        break;
      } else {
        if (strcmp(rsp.msg, "no match") == 0) {
          audioAccessDenied();
          displayWord("Invalid", AccessModeHeader);
          delay(1000);
        }

      }
    }
    //anyone can lock the door, but ask again to confirm if they want to lock it
  }
}

bool verifyMasterPermission(const char *modeHeader) {
  if (enrolledUsers == 0) {
    return true;  // Bypass master check if no users exist
  }

  debugf("Verifying Master permission for mode: %s\n", modeHeader);
  display_three_words("Place", "Master", "Finger", 2);

  while (1) {
    if (exit()) {
      debugln("Master check cancelled by user.");
      return false;
    }

    SensorRsp rsp = getFingerprintID();
    if (!rsp.isError) {
      char *endptr;
      uint16_t id = (uint16_t)strtol(rsp.msg, &endptr, 10);
      UserRecord master = getUser(id);
      if (master.notMaster == 0) {
        debugf("Master verified: %s\n", master.name);
        audioAccessGranted();
        displayTwoWords("Master", master.name, modeHeader);
        delay(1000);
        return true;
      } else {
        debugln("Scanned user is not a Master.");
        audioAccessDenied();
        displayTwoWords("Not", "Master", modeHeader);
        delay(1000);
        display_three_words("Place", "Master", "Finger", 2);
      }
    } else {
      if (strcmp(rsp.msg, "no match") == 0) {
        debugln("No match found for Master verification.");
        audioAccessDenied();
        displayTwoWords("No", "Match", modeHeader);
        delay(1000);
        display_three_words("Place", "Master", "Finger", 2);
      }
    }
    delay(50);
  }
}

// bool checkAccess(UserRecord *&user) {
//   SensorRsp rsp = getFingerprintID();
//   if (!rsp.isError) {
//     char *endptr;
//     uint16_t id = (uint16_t)strtol(rsp.msg, &endptr, 10);

//     user = getUser(id);  // This will now modify the caller's pointer!
//     return true;
//   }
//   return false;
// }

bool exit() {
  bool buttonState = RIGHT_BUTTON.pressed();
  if (buttonState) {
    audioBeep();
    mode = MENU;
  }
  return buttonState;
}