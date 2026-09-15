#include <Arduino.h>
#include "esp_log.h"

#define GREEN_LED1_PIN 15
#define GREEN_LED2_PIN 16
#define GREEN_LED3_PIN 17
#define GREEN_LED4_PIN 18
#define GREEN_LED5_PIN 8
#define BLUE_LED1_PIN 6
#define BLUE_LED2_PIN 7
#define PHOTO_PIN 4
#define POT_PIN 5
#define BUTTON1_PIN 9
#define BUTTON2_PIN 10
#define DEBOUNCE 50
#define LEVEL_THRESHOLD 20
#define ZERO_BRIGHTNESS_THRESHOLD 50

template <typename T, std::size_t N>
constexpr std::size_t arraySize(const T (&)[N]) noexcept {
  return N;
}

namespace UserData
{
  enum sourceMode {
    pot,
    ldr,
  };

  enum showMode {
    level,
    brightness,
    animation,
  };

  struct LedState {
    bool isOn { false };
    uint8_t brightness { 255 };

    LedState() = default;
    LedState(bool on, uint8_t brightness)
      : isOn(on), brightness(brightness)
      {}
  };


  LedState blue[] {
    { true, 255 },
    { false, 255 },
  };

  LedState green[] {
    { false, 255 },
    { false, 255 },
    { false, 255 },
    { false, 255 },
    { false, 255 },
  };

  struct AnimationStep {
    uint32_t timestamp;
    uint8_t brightness;
    bool transition;
  };

  class LedAnimation {
    private:
      const AnimationStep* m_steps;
      uint8_t m_totalSteps;
      uint8_t m_currentStep { 0 };
      LedState& m_ledState;

      uint32_t m_startTime { 0 };
      uint32_t m_duration;
      uint32_t m_previousDiffTime { 0 };

      const AnimationStep& getCurrentStep() const {
        return m_steps[m_currentStep];
      }

      const AnimationStep& getNextStep() const {
        return m_steps[m_currentStep >= m_totalSteps - 1 ? 0 : m_currentStep + 1];
      }

    public:
      LedAnimation(uint32_t duration, const AnimationStep* steps, uint8_t totalSteps, LedState& ledState)
      : m_duration { duration }, m_steps { steps }, m_totalSteps { totalSteps }, m_ledState { ledState }
      {}

      void start(uint32_t startTime) {
        m_startTime = startTime;
        m_currentStep = 0;
      }

      void updateLedState(uint32_t currentTime) {
        const uint32_t diffTime = (currentTime - m_startTime) % m_duration;
        const AnimationStep& currentStep = getCurrentStep();
        const AnimationStep& nextStep = getNextStep();

        const bool isLastStep = m_currentStep == m_totalSteps - 1;

        // Starting the next iteration cycle
        if (diffTime < m_previousDiffTime) {
          m_currentStep = 0;
          m_ledState.brightness = m_steps[0].brightness;
          m_previousDiffTime = 0;
          return;
        }

        m_previousDiffTime = diffTime;
        
        // Moving to the next animation step
        if (diffTime >= nextStep.timestamp && !isLastStep) {
          m_currentStep = (m_currentStep + 1) % m_totalSteps;
          m_ledState.brightness = nextStep.brightness;
          return;
        }

        if (nextStep.timestamp == currentStep.timestamp) return;

        // Calculating new brightness with smooth transition
        if (currentStep.transition) {
          const int16_t brightnessDiff = (int16_t)nextStep.brightness - (int16_t)currentStep.brightness;
          const uint32_t stepDuration = (isLastStep ? m_duration : nextStep.timestamp) - currentStep.timestamp;
          const uint32_t timePassed = diffTime - currentStep.timestamp;
          
          const uint8_t newBrightness = currentStep.brightness + (brightnessDiff * (int32_t)timePassed) / (int32_t)stepDuration;
          m_ledState.isOn = true;
          m_ledState.brightness = newBrightness;
        }
      }
    
  };

  constexpr uint32_t animationDuration { 1000 };

  const AnimationStep led1AnimationPattern[] {
    { 0, 0, false },
    { 100, 255, false },
    { 200, 100, true },
    { 300, 0, false },
    { 900, 255, false },
  };
  const AnimationStep led2AnimationPattern[] {
    { 0, 0, false },
    { 200, 255, false },
    { 300, 100, true },
    { 400, 0, false },
    { 800, 255, false },
    { 900, 100, true },
  };
  const AnimationStep led3AnimationPattern[] {
    { 0, 0, false },
    { 300, 255, false },
    { 400, 100, true },
    { 500, 0, false },
    { 700, 255, false },
    { 800, 100, true },
    { 900, 0, false },
  };
  const AnimationStep led4AnimationPattern[] {
    { 0, 0, false },
    { 400, 255, false },
    { 500, 100, true },
    { 599, 0, false },
    { 600, 255, false },
    { 700, 100, true },
    { 800, 0, false },
  };
  const AnimationStep led5AnimationPattern[] {
    { 0, 0, false },
    { 500, 255, false },
    { 600, 100, true },
    { 700, 0, false },
  };

  LedAnimation led1Animation(animationDuration, led1AnimationPattern, arraySize(led1AnimationPattern), green[0]);
  LedAnimation led2Animation(animationDuration, led2AnimationPattern, arraySize(led2AnimationPattern), green[1]);
  LedAnimation led3Animation(animationDuration, led3AnimationPattern, arraySize(led3AnimationPattern), green[2]);
  LedAnimation led4Animation(animationDuration, led4AnimationPattern, arraySize(led4AnimationPattern), green[3]);
  LedAnimation led5Animation(animationDuration, led5AnimationPattern, arraySize(led5AnimationPattern), green[4]);

  sourceMode currentSourceMode { sourceMode::pot };
  showMode currentShowMode { showMode::level };

  uint32_t smoothingBuffer[3] {};
  constexpr uint8_t bufferSize { 3 };
  uint8_t bufferElementCount { 0 };
  uint8_t currentBufferIndex { 0 };

  void nextIndex() {
    currentBufferIndex = (currentBufferIndex + 1) % bufferSize;
  }

  void addValueToBuffer(uint32_t value) {
    smoothingBuffer[currentBufferIndex] = value;
    nextIndex();
    if (bufferElementCount < 3) {
      bufferElementCount++;
    }
  }

  bool checkIfBufferFull() {
    return bufferElementCount == bufferSize;
  }

  void resetBuffer() {
    bufferElementCount = 0;
  }

  uint32_t getSmoothedValue() {
    if (!checkIfBufferFull()) return 0;

    uint32_t sum { 0 };

    for (uint8_t i { 0 }; i < bufferSize; ++i) {
      sum += smoothingBuffer[i];
    }

    return sum / bufferSize;
  }

  constexpr uint8_t NO_LEVEL = 255;
  uint8_t previousLevel { NO_LEVEL };

  void switchSourceMode() {
    switch (currentSourceMode) {
      case sourceMode::ldr: {
        currentSourceMode = sourceMode::pot;
        blue[0] = { true, 255 };
        blue[1] = { false, 255 };
      } break;
      case sourceMode::pot: {
        currentSourceMode = sourceMode::ldr;
        blue[0] = { false, 255 };
        blue[1] = { true, 255 };
      } break;
    }
    if (currentShowMode == showMode::animation) {
      currentShowMode = showMode::level;
    }
    resetBuffer();
    previousLevel = NO_LEVEL;
  }

  void switchShowMode() {
    switch (currentShowMode) {
      case showMode::level: {
        currentShowMode = showMode::brightness;
      } break;
      case showMode::brightness: {
        currentShowMode = showMode::level;
      } break;
      case showMode::animation: {
        currentShowMode = showMode::level;
        currentSourceMode = sourceMode::pot;
        blue[0] = { true, 255 };
        blue[1] = { false, 255 };
      } break;
    }
    resetBuffer();
    previousLevel = NO_LEVEL;
  }

  uint32_t startAnimationTime { 0 };

  void switchToAnimation() {
    currentShowMode = showMode::animation;
    blue[0] = { true, 255 };
    blue[1] = { true, 255 };
    startAnimationTime = millis();
    led1Animation.start(startAnimationTime);
    led2Animation.start(startAnimationTime);
    led3Animation.start(startAnimationTime);
    led4Animation.start(startAnimationTime);
    led5Animation.start(startAnimationTime);
    resetBuffer();
  }

  bool isSourceModeButtonPressed { false };
  uint32_t sourceModeButtonPressTime { 0 };
  uint32_t sourceModeButtonReleaseTime { 0 };
  bool isShowModeButtonPressed { false };
  uint32_t showModeButtonPressTime { 0 };
  uint32_t showModeButtonReleaseTime { 0 };
  uint32_t showAnimationTime { 0 };
}

void onSourceModeButtonPress() {
  if (UserData::isSourceModeButtonPressed) return;

  const uint32_t now = millis();

  if (UserData::isShowModeButtonPressed) {
    UserData::switchToAnimation();
    UserData::isSourceModeButtonPressed = true;
    UserData::sourceModeButtonPressTime = now;
    UserData::showAnimationTime = now;
    return;
  }

  
  if ((now - UserData::sourceModeButtonReleaseTime) < DEBOUNCE) return;

  UserData::isSourceModeButtonPressed = true;
  UserData::sourceModeButtonPressTime = now;
}

void onSourceModeButtonRelease() {
  if (!UserData::isSourceModeButtonPressed) return;

  const uint32_t now = millis();

  if ((now - UserData::sourceModeButtonReleaseTime) < DEBOUNCE) return;

  if ((now - UserData::sourceModeButtonPressTime) < DEBOUNCE) return;

  if (
    UserData::showAnimationTime < UserData::sourceModeButtonPressTime &&
    UserData::isShowModeButtonPressed == false
  ) {
    UserData::switchSourceMode();
  }

  UserData::sourceModeButtonReleaseTime = now;
  UserData::isSourceModeButtonPressed = false;
}

void IRAM_ATTR onSourceModeButtonChange() {
  const uint32_t value = digitalRead(BUTTON1_PIN);

  if (value) {
    onSourceModeButtonRelease();
  } else {
    onSourceModeButtonPress();
  }
}

void onShowModeButtonPress() {
  if (UserData::isShowModeButtonPressed) return;

  const uint32_t now = millis();

  if (UserData::isSourceModeButtonPressed) {
    UserData::switchToAnimation();
    UserData::isShowModeButtonPressed = true;
    UserData::showModeButtonPressTime = now;
    UserData::showAnimationTime = now;
    return;
  }

  
  if ((now - UserData::showModeButtonReleaseTime) < DEBOUNCE) return;

  UserData::isShowModeButtonPressed = true;
  UserData::showModeButtonPressTime = now;
}

void onShowModeButtonRelease() {
  if (!UserData::isShowModeButtonPressed) return;

  const uint32_t now = millis();

  if ((now - UserData::showModeButtonReleaseTime) < DEBOUNCE) return;

  if ((now - UserData::showModeButtonPressTime) < DEBOUNCE) return;

  if (
    UserData::showAnimationTime < UserData::showModeButtonPressTime &&
    UserData::isSourceModeButtonPressed == false
  ) {
    UserData::switchShowMode();
  }

  UserData::showModeButtonReleaseTime = now;
  UserData::isShowModeButtonPressed = false;
}

void IRAM_ATTR onShowModeButtonChange() {
  const uint32_t value = digitalRead(BUTTON2_PIN);

  if (value) {
    onShowModeButtonRelease();
  } else {
    onShowModeButtonPress();
  }
}

void updateStateMachine() {
  uint32_t value {};

  if (UserData::currentShowMode == UserData::showMode::animation) {
    const uint32_t now = millis();
    UserData::led1Animation.updateLedState(now);
    UserData::led2Animation.updateLedState(now);
    UserData::led3Animation.updateLedState(now);
    UserData::led4Animation.updateLedState(now);
    UserData::led5Animation.updateLedState(now);
    return;
  }

  switch (UserData::currentSourceMode) {
    case UserData::sourceMode::ldr: {
      value = analogRead(PHOTO_PIN);
    } break;
    case UserData::sourceMode::pot: {
      value = analogRead(POT_PIN);
    } break;
  }

  UserData::addValueToBuffer(value);
  if (!UserData::checkIfBufferFull()) {
    Serial.println("Buffer is not full! Exit!");
    return;
  }

  const uint32_t smoothedValue = UserData::getSmoothedValue();

  switch (UserData::currentShowMode) {
    case UserData::showMode::animation: {
    } break;
    case UserData::showMode::brightness: {
      uint32_t brightness = map(smoothedValue <= ZERO_BRIGHTNESS_THRESHOLD ? 0 : smoothedValue, 0, 4095, 0, 255);
      brightness = constrain(brightness, 0, 255);
      Serial.print("new brightness: ");
      Serial.println(brightness);
      for (size_t i = 0; i < arraySize(UserData::green); ++i) {
        UserData::green[i] = { true, brightness };
      }
      UserData::previousLevel = UserData::NO_LEVEL;

    } break;
    case UserData::showMode::level: {
      uint32_t level = smoothedValue * (arraySize(UserData::green) + 1) / 4096;
      Serial.print("level: ");
      Serial.println(level);
      if (level == UserData::previousLevel) return;

      if (
        (level > UserData::previousLevel &&
        smoothedValue > ((UserData::previousLevel + 1) * 4095) / 6 + LEVEL_THRESHOLD) ||
        (level < UserData::previousLevel &&
        smoothedValue + LEVEL_THRESHOLD < (UserData::previousLevel * 4095 / 6))
      )
      {
        Serial.print("New level: ");
        Serial.println(level);
        for (size_t i = 0; i < arraySize(UserData::green); ++i) {
          UserData::green[i] = { i < level, 255 };
        }

        UserData::previousLevel = level;
      }
    } break;
  }
}

void updateGreenLeds() {
  analogWrite(GREEN_LED1_PIN, UserData::green[0].isOn ? UserData::green[0].brightness : 0);
  analogWrite(GREEN_LED2_PIN, UserData::green[1].isOn ? UserData::green[1].brightness : 0);
  analogWrite(GREEN_LED3_PIN, UserData::green[2].isOn ? UserData::green[2].brightness : 0);
  analogWrite(GREEN_LED4_PIN, UserData::green[3].isOn ? UserData::green[3].brightness : 0);
  analogWrite(GREEN_LED5_PIN, UserData::green[4].isOn ? UserData::green[4].brightness : 0);
}

void updateBlueLeds() {
  analogWrite(BLUE_LED1_PIN, UserData::blue[0].isOn ? 20 : 0);
  analogWrite(BLUE_LED2_PIN, UserData::blue[1].isOn ? 20 : 0);
}

void setup() {
  Serial.begin(115200);
  Serial.println("== setup ==");

  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON1_PIN), onSourceModeButtonChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(BUTTON2_PIN), onShowModeButtonChange, CHANGE);
}

void loop() {
  updateStateMachine();
  updateGreenLeds();
  updateBlueLeds();

  delay(10);
}
