/*
  xsns_68_mcp230xx.ino - Support for I2C MCP23008/MCP23017 GPIO Expander on Tasmota

  Copyright (C) 2020  Andre Thomas and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_I2C
#ifdef USE_MCP230xx1
/*********************************************************************************************\
   MCP23008/17 - I2C GPIO EXPANDER

   Docs at https://www.microchip.com/wwwproducts/en/MCP23008
           https://www.microchip.com/wwwproducts/en/MCP23017

   I2C Address: 0x20 - 0x26 (0x27 is not supported)
\*********************************************************************************************/

#define XSNS_68                   68
#define XI2C_22                   22  // See I2CDEVICES.md

/*
   Default register locations for MCP23008 - They change for MCP23017 in default bank mode
*/

uint8_t MCP230xx1_IODIR          = 0x00;
uint8_t MCP230xx1_GPINTEN        = 0x02;
uint8_t MCP230xx1_IOCON          = 0x05;
uint8_t MCP230xx1_GPPU           = 0x06;
uint8_t MCP230xx1_INTF           = 0x07;
uint8_t MCP230xx1_INTCAP         = 0x08;
uint8_t MCP230xx1_GPIO           = 0x09;

uint8_t MCP230xx1_type = 0;
uint8_t MCP230xx1_pincount = 0;
uint8_t MCP230xx1_int_en = 0;
uint8_t MCP230xx1_int_prio_counter = 0;
uint8_t MCP230xx1_int_counter_en = 0;
uint8_t MCP230xx1_int_retainer_en = 0;
uint8_t MCP230xx1_int_sec_counter = 0;

uint8_t MCP230xx1_int_report_defer_counter[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

uint16_t MCP230xx1_int_counter[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

uint8_t MCP230xx1_int_retainer[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; // Used to store if an interrupt occured that needs to be retained until teleperiod

unsigned long int_millis1[16]; // To keep track of millis() since last interrupt

const char MCP230xx1_SENSOR_RESPONSE[] PROGMEM = "{\"Sensor68_D%i\":{\"MODE\":%i,\"PULL_UP\":\"%s\",\"INT_MODE\":\"%s\",\"STATE\":\"%s\"}}";

const char MCP230xx1_INTCFG_RESPONSE[] PROGMEM = "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";

#ifdef USE_MCP230xx1_OUTPUT
const char MCP230xx1_CMND_RESPONSE[] PROGMEM = "{\"S68cmnd_D%i\":{\"COMMAND\":\"%s\",\"STATE\":\"%s\"}}";
#endif // USE_MCP230xx1_OUTPUT

void MCP230xx1_CheckForIntCounter(void) {
  uint8_t en = 0;
  for (uint32_t ca=0;ca<16;ca++) {
    if (Settings.mcp230xx1_config[ca].int_count_en) {
      en=1;
    }
  }
  if (!Settings.mcp230xx1_int_timer) en=0;
  MCP230xx1_int_counter_en=en;
  if (!MCP230xx1_int_counter_en) { // Interrupt counters are disabled, so we clear all the counters
    for (uint32_t ca=0;ca<16;ca++) {
      MCP230xx1_int_counter[ca] = 0;
    }
  }
}

void MCP230xx1_CheckForIntRetainer(void) {
  uint8_t en = 0;
  for (uint32_t ca=0;ca<16;ca++) {
    if (Settings.mcp230xx1_config[ca].int_retain_flag) {
      en=1;
    }
  }
  MCP230xx1_int_retainer_en=en;
  if (!MCP230xx1_int_retainer_en) { // Interrupt counters are disabled, so we clear all the counters
    for (uint32_t ca=0;ca<16;ca++) {
      MCP230xx1_int_retainer[ca] = 0;
    }
  }
}

const char* ConvertNumTxt1(uint8_t statu, uint8_t pinmod=0) {
#ifdef USE_MCP230xx1_OUTPUT
if ((6 == pinmod) && (statu < 2)) { statu = abs(statu-1); }
#endif // USE_MCP230xx1_OUTPUT
  switch (statu) {
    case 0:
      return "OFF";
      break;
    case 1:
      return "ON";
      break;
#ifdef USE_MCP230xx1_OUTPUT
    case 2:
      return "TOGGLE";
      break;
#endif // USE_MCP230xx1_OUTPUT
  }
  return "";
}

const char* IntModeTxt1(uint8_t intmo) {
  switch (intmo) {
    case 0:
      return "ALL";
      break;
    case 1:
      return "EVENT";
      break;
    case 2:
      return "TELE";
      break;
    case 3:
      return "DISABLED";
      break;
  }
  return "";
}

uint8_t MCP230xx1_readGPIO(uint8_t port) {
  return I2cRead8(USE_MCP230xx1_ADDR, MCP230xx1_GPIO + port);
}

void MCP230xx1_ApplySettings(void)
{
  uint8_t int_en = 0;
  for (uint32_t MCP230xx1_port = 0; MCP230xx1_port < MCP230xx1_type; MCP230xx1_port++) {
    uint8_t reg_gppu = 0;
    uint8_t reg_gpinten = 0;
    uint8_t reg_iodir = 0xFF;
#ifdef USE_MCP230xx1_OUTPUT
    uint8_t reg_portpins = 0x00;
#endif // USE_MCP230xx1_OUTPUT
    for (uint32_t idx = 0; idx < 8; idx++) {
      switch (Settings.mcp230xx1_config[idx+(MCP230xx1_port*8)].pinmode) {
        case 0 ... 1:
          reg_iodir |= (1 << idx);
          break;
        case 2 ... 4:
          reg_iodir |= (1 << idx);
          reg_gpinten |= (1 << idx);
          int_en = 1;
          break;
#ifdef USE_MCP230xx1_OUTPUT
        case 5 ... 6:
          reg_iodir &= ~(1 << idx);
          if (Settings.flag.save_state) {  // SetOption0 - Save power state and use after restart - Firmware configuration wants us to use the last pin state
            reg_portpins |= (Settings.mcp230xx1_config[idx+(MCP230xx1_port*8)].saved_state << idx);
          } else {
            if (Settings.mcp230xx1_config[idx+(MCP230xx1_port*8)].pullup) {
              reg_portpins |= (1 << idx);
            }
          }
          break;
#endif // USE_MCP230xx1_OUTPUT
        default:
          break;
      }
#ifdef USE_MCP230xx1_OUTPUT
      if ((Settings.mcp230xx1_config[idx+(MCP230xx1_port*8)].pullup) && (Settings.mcp230xx1_config[idx+(MCP230xx1_port*8)].pinmode < 5)) {
        reg_gppu |= (1 << idx);
      }
#else // not USE_MCP230xx1_OUTPUT
      if (Settings.mcp230xx1_config[idx+(MCP230xx1_port*8)].pullup) {
        reg_gppu |= (1 << idx);
      }
#endif // USE_MCP230xx1_OUTPUT
    }
    I2cWrite8(USE_MCP230xx1_ADDR, MCP230xx1_GPPU+MCP230xx1_port, reg_gppu);
    I2cWrite8(USE_MCP230xx1_ADDR, MCP230xx1_GPINTEN+MCP230xx1_port, reg_gpinten);
    I2cWrite8(USE_MCP230xx1_ADDR, MCP230xx1_IODIR+MCP230xx1_port, reg_iodir);
#ifdef USE_MCP230xx1_OUTPUT
    I2cWrite8(USE_MCP230xx1_ADDR, MCP230xx1_GPIO+MCP230xx1_port, reg_portpins);
#endif // USE_MCP230xx1_OUTPUT
  }
  for (uint32_t idx=0;idx<MCP230xx1_pincount;idx++) {
    int_millis1[idx]=millis();
  }
  MCP230xx1_int_en = int_en;
  MCP230xx1_CheckForIntCounter();  // update register on whether or not we should be counting interrupts
  MCP230xx1_CheckForIntRetainer(); // update register on whether or not we should be retaining interrupt events for teleperiod
}

void MCP230xx1_Detect(void)
{
  if (I2cActive(USE_MCP230xx1_ADDR)) { return; }

  uint8_t buffer;

  I2cWrite8(USE_MCP230xx1_ADDR, MCP230xx1_IOCON, 0x80); // attempt to set bank mode - this will only work on MCP23017, so its the best way to detect the different chips 23008 vs 23017
  if (I2cValidRead8(&buffer, USE_MCP230xx1_ADDR, MCP230xx1_IOCON)) {
    if (0x00 == buffer) {
      MCP230xx1_type = 1; // We have a MCP23008
      I2cSetActiveFound(USE_MCP230xx1_ADDR, "MCP23008");
      MCP230xx1_pincount = 8;
      MCP230xx1_ApplySettings();
    } else {
      if (0x80 == buffer) {
        MCP230xx1_type = 2; // We have a MCP23017
        I2cSetActiveFound(USE_MCP230xx1_ADDR, "MCP23017");
        MCP230xx1_pincount = 16;
        // Reset bank mode to 0
        I2cWrite8(USE_MCP230xx1_ADDR, MCP230xx1_IOCON, 0x00);
        // Update register locations for MCP23017
        MCP230xx1_GPINTEN        = 0x04;
        MCP230xx1_GPPU           = 0x0C;
        MCP230xx1_INTF           = 0x0E;
        MCP230xx1_INTCAP         = 0x10;
        MCP230xx1_GPIO           = 0x12;
        MCP230xx1_ApplySettings();
      }
    }
  }
}

void MCP230xx1_CheckForInterrupt(void) {
  uint8_t intf;
  uint8_t MCP230xx1_intcap = 0;
  uint8_t report_int;
  for (uint32_t MCP230xx1_port = 0; MCP230xx1_port < MCP230xx1_type; MCP230xx1_port++) {
    if (I2cValidRead8(&intf,USE_MCP230xx1_ADDR,MCP230xx1_INTF+MCP230xx1_port)) {
      if (intf > 0) {
        if (I2cValidRead8(&MCP230xx1_intcap, USE_MCP230xx1_ADDR, MCP230xx1_INTCAP+MCP230xx1_port)) {
          for (uint32_t intp = 0; intp < 8; intp++) {
            if ((intf >> intp) & 0x01) { // we know which pin caused interrupt
              report_int = 0;
              if (Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].pinmode > 1) {
                switch (Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].pinmode) {
                  case 2:
                    report_int = 1;
                    break;
                  case 3:
                    if (((MCP230xx1_intcap >> intp) & 0x01) == 0) report_int = 1; // Int on LOW
                    break;
                  case 4:
                    if (((MCP230xx1_intcap >> intp) & 0x01) == 1) report_int = 1; // Int on HIGH
                    break;
                  default:
                    break;
                }
                // Check for interrupt counter
                if ((MCP230xx1_int_counter_en) && (report_int)) { // We may have some counting to do
                  if (Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].int_count_en) { // Indeed, for this pin
                    MCP230xx1_int_counter[intp+(MCP230xx1_port*8)]++;
                  }
                }
                // check for interrupt defer on this pin
                if (report_int) {
                  if (Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].int_report_defer) {
                    MCP230xx1_int_report_defer_counter[intp+(MCP230xx1_port*8)]++;
                    if (MCP230xx1_int_report_defer_counter[intp+(MCP230xx1_port*8)] >= Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].int_report_defer) {
                      MCP230xx1_int_report_defer_counter[intp+(MCP230xx1_port*8)]=0;
                    } else {
                      report_int = 0; // defer int report for now
                    }
                  }
                }
                // check if interrupt retain is used, if it is for this pin then we do not report immediately as it will be reported in teleperiod
                if (report_int) {
                  if (Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].int_retain_flag) {
                    MCP230xx1_int_retainer[intp+(MCP230xx1_port*8)] = 1;
                    report_int = 0; // do not report for now
                  }
                }
                if (Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].int_count_en) { // We do not want to report via tele or event if counting is enabled
                  report_int = 0;
                }
                if (report_int) {
                  bool int_tele = false;
                  bool int_event = false;
                  unsigned long millis_now = millis();
                  unsigned long millis_since_last_int = millis_now - int_millis1[intp+(MCP230xx1_port*8)];
                  int_millis1[intp+(MCP230xx1_port*8)]=millis_now;
                  switch (Settings.mcp230xx1_config[intp+(MCP230xx1_port*8)].int_report_mode) {
                    case 0:
                      int_tele=true;
                      int_event=true;
                      break;
                    case 1:
                      int_event=true;
                      break;
                    case 2:
                      int_tele=true;
                      break;
                  }
                  if (int_tele) {
                    ResponseTime_P(PSTR(",\"MCP230xx1_INT\":{\"D%i\":%i,\"MS\":%lu}}"),
                      intp+(MCP230xx1_port*8), ((MCP230xx1_intcap >> intp) & 0x01),millis_since_last_int);
                    MqttPublishPrefixTopic_P(RESULT_OR_STAT, PSTR("MCP230xx1_INT"));
                  }
                  if (int_event) {
                    char command[19]; // Theoretical max = 'event MCPINT_D16=1' so 18 + 1 (for the \n)
                    sprintf(command,"event MCPINT_D%i=%i",intp+(MCP230xx1_port*8),((MCP230xx1_intcap >> intp) & 0x01));
                    ExecuteCommand(command, SRC_RULE);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

void MCP230xx1_Show(bool json)
{
  if (json) {
    uint8_t gpio = MCP230xx1_readGPIO(0);
    ResponseAppend_P(PSTR(",\"MCP230XX\":{\"D0\":%i,\"D1\":%i,\"D2\":%i,\"D3\":%i,\"D4\":%i,\"D5\":%i,\"D6\":%i,\"D7\":%i"),
                (gpio>>0)&1,(gpio>>1)&1,(gpio>>2)&1,(gpio>>3)&1,(gpio>>4)&1,(gpio>>5)&1,(gpio>>6)&1,(gpio>>7)&1);
    if (2 == MCP230xx1_type) {
      gpio = MCP230xx1_readGPIO(1);
      ResponseAppend_P(PSTR(",\"D8\":%i,\"D9\":%i,\"D10\":%i,\"D11\":%i,\"D12\":%i,\"D13\":%i,\"D14\":%i,\"D15\":%i"),
                  (gpio>>0)&1,(gpio>>1)&1,(gpio>>2)&1,(gpio>>3)&1,(gpio>>4)&1,(gpio>>5)&1,(gpio>>6)&1,(gpio>>7)&1);
    }
    ResponseJsonEnd();
  }
}

#ifdef USE_MCP230xx1_OUTPUT

void MCP230xx1_SetOutPin(uint8_t pin,uint8_t pinstate) {
  uint8_t portpins;
  uint8_t port = 0;
  uint8_t pinmo = Settings.mcp230xx1_config[pin].pinmode;
  uint8_t interlock = Settings.flag.interlock;  // CMND_INTERLOCK - Enable/disable interlock
  int pinadd = (pin % 2)+1-(3*(pin % 2)); //check if pin is odd or even and convert to 1 (if even) or -1 (if odd)
  char cmnd[7], stt[4];
  if (pin > 7) { port = 1; }
  portpins = MCP230xx1_readGPIO(port);
  if (interlock && (pinmo == Settings.mcp230xx1_config[pin+pinadd].pinmode)) {
    if (pinstate < 2) {
      if (6 == pinmo) {
        if (pinstate) portpins |= (1 << (pin-(port*8))); else portpins |= (1 << (pin+pinadd-(port*8))),portpins &= ~(1 << (pin-(port*8)));
      } else {
        if (pinstate) portpins &= ~(1 << (pin+pinadd-(port*8))),portpins |= (1 << (pin-(port*8))); else portpins &= ~(1 << (pin-(port*8)));
      }
    } else {
      if (6 == pinmo) {
      portpins |= (1 << (pin+pinadd-(port*8))),portpins ^= (1 << (pin-(port*8)));
      } else {
      portpins &= ~(1 << (pin+pinadd-(port*8))),portpins ^= (1 << (pin-(port*8)));
      }
    }
  } else {
    if (pinstate < 2) {
      if (pinstate) portpins |= (1 << (pin-(port*8))); else portpins &= ~(1 << (pin-(port*8)));
    } else {
      portpins ^= (1 << (pin-(port*8)));
    }
  }
  I2cWrite8(USE_MCP230xx1_ADDR, MCP230xx1_GPIO + port, portpins);
  if (Settings.flag.save_state) {  // SetOption0 - Save power state and use after restart - Firmware configured to save last known state in settings
    Settings.mcp230xx1_config[pin].saved_state=portpins>>(pin-(port*8))&1;
    Settings.mcp230xx1_config[pin+pinadd].saved_state=portpins>>(pin+pinadd-(port*8))&1;
  }
  sprintf(cmnd,ConvertNumTxt(pinstate, pinmo));
  sprintf(stt,ConvertNumTxt((portpins >> (pin-(port*8))&1), pinmo));
  if (interlock && (pinmo == Settings.mcp230xx1_config[pin+pinadd].pinmode)) {
    char stt1[4];
    sprintf(stt1,ConvertNumTxt((portpins >> (pin+pinadd-(port*8))&1), pinmo));
    Response_P(PSTR("{\"S68cmnd_D%i\":{\"COMMAND\":\"%s\",\"STATE\":\"%s\"},\"S68cmnd_D%i\":{\"STATE\":\"%s\"}}"),pin, cmnd, stt, pin+pinadd, stt1);
  } else {
    Response_P(MCP230xx1_CMND_RESPONSE, pin, cmnd, stt);
  }
}

#endif // USE_MCP230xx1_OUTPUT

void MCP230xx1_Reset(uint8_t pinmode) {
  uint8_t pullup = 0;
  if ((pinmode > 1) && (pinmode < 5)) { pullup=1; }
  for (uint32_t pinx=0;pinx<16;pinx++) {
    Settings.mcp230xx1_config[pinx].pinmode=pinmode;
    Settings.mcp230xx1_config[pinx].pullup=pullup;
    Settings.mcp230xx1_config[pinx].saved_state=0;
    if ((pinmode > 1) && (pinmode < 5)) {
      Settings.mcp230xx1_config[pinx].int_report_mode=0; // Enabled for ALL by default
    } else {
      Settings.mcp230xx1_config[pinx].int_report_mode=3; // Disabled for pinmode 1, 5 and 6 (No interrupts there)
    }
    Settings.mcp230xx1_config[pinx].int_report_defer=0; // Disabled
    Settings.mcp230xx1_config[pinx].int_count_en=0;     // Disabled by default
    Settings.mcp230xx1_config[pinx].int_retain_flag=0;  // Disabled by default
    Settings.mcp230xx1_config[pinx].spare13=0;
    Settings.mcp230xx1_config[pinx].spare14=0;
    Settings.mcp230xx1_config[pinx].spare15=0;
  }
  Settings.mcp230xx1_int_prio = 0; // Once per FUNC_EVERY_50_MSECOND callback
  Settings.mcp230xx1_int_timer = 0;
  MCP230xx1_ApplySettings();
  char pulluptxt[7];
  char intmodetxt[9];
  sprintf(pulluptxt,ConvertNumTxt(pullup));
  uint8_t intmode = 3;
  if ((pinmode > 1) && (pinmode < 5)) { intmode = 0; }
  sprintf(intmodetxt,IntModeTxt1(intmode));
  Response_P(MCP230xx1_SENSOR_RESPONSE,99,pinmode,pulluptxt,intmodetxt,"");
}

bool MCP230xx1_Command(void)
{
  bool serviced = true;
  bool validpin = false;
  uint8_t paramcount = 0;
  if (XdrvMailbox.data_len > 0) {
    paramcount=1;
  } else {
    serviced = false;
    return serviced;
  }
  char sub_string[XdrvMailbox.data_len];
  for (uint32_t ca=0;ca<XdrvMailbox.data_len;ca++) {
    if ((' ' == XdrvMailbox.data[ca]) || ('=' == XdrvMailbox.data[ca])) { XdrvMailbox.data[ca] = ','; }
    if (',' == XdrvMailbox.data[ca]) { paramcount++; }
  }
  UpperCase(XdrvMailbox.data,XdrvMailbox.data);
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET"))  {  MCP230xx1_Reset(1); return serviced; }
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET1")) {  MCP230xx1_Reset(1); return serviced; }
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET2")) {  MCP230xx1_Reset(2); return serviced; }
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET3")) {  MCP230xx1_Reset(3); return serviced; }
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET4")) {  MCP230xx1_Reset(4); return serviced; }
#ifdef USE_MCP230xx1_OUTPUT
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET5")) {  MCP230xx1_Reset(5); return serviced; }
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET6")) {  MCP230xx1_Reset(6); return serviced; }
#endif // USE_MCP230xx1_OUTPUT

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"INTPRI")) {
    if (paramcount > 1) {
      uint8_t intpri = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
      if ((intpri >= 0) && (intpri <= 20)) {
        Settings.mcp230xx1_int_prio = intpri;
        Response_P(MCP230xx1_INTCFG_RESPONSE,"PRI",99,Settings.mcp230xx1_int_prio);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
        return serviced;
      }
    } else { // No parameter was given for INTPRI so we return the current configured value
      Response_P(MCP230xx1_INTCFG_RESPONSE,"PRI",99,Settings.mcp230xx1_int_prio);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
      return serviced;
    }
  }

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"INTTIMER")) {
    if (paramcount > 1) {
      uint8_t inttim = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
      if ((inttim >= 0) && (inttim <= 3600)) {
        Settings.mcp230xx1_int_timer = inttim;
        MCP230xx1_CheckForIntCounter(); // update register on whether or not we should be counting interrupts
        Response_P(MCP230xx1_INTCFG_RESPONSE,"TIMER",99,Settings.mcp230xx1_int_timer);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
        return serviced;
      }
    } else { // No parameter was given for INTTIM so we return the current configured value
      Response_P(MCP230xx1_INTCFG_RESPONSE,"TIMER",99,Settings.mcp230xx1_int_timer);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
      return serviced;
    }
  }

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"INTDEF")) {
    if (paramcount > 1) {
      uint8_t pin = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
      if (pin < MCP230xx1_pincount) {
        if (pin == 0) {
          if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "0")) validpin=true;
        } else {
          validpin = true;
        }
      }
      if (validpin) {
        if (paramcount > 2) {
          uint8_t intdef = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
          if ((intdef >= 0) && (intdef <= 15)) {
            Settings.mcp230xx1_config[pin].int_report_defer=intdef;
            if (Settings.mcp230xx1_config[pin].int_count_en) {
              Settings.mcp230xx1_config[pin].int_count_en=0;
              MCP230xx1_CheckForIntCounter();
              AddLog_P2(LOG_LEVEL_INFO, PSTR("*** WARNING *** - Disabled INTCNT for pin D%i"),pin);
            }
            Response_P(MCP230xx1_INTCFG_RESPONSE,"DEF",pin,Settings.mcp230xx1_config[pin].int_report_defer);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
            return serviced;
          } else {
            serviced=false;
            return serviced;
          }
        } else {
          Response_P(MCP230xx1_INTCFG_RESPONSE,"DEF",pin,Settings.mcp230xx1_config[pin].int_report_defer);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
          return serviced;
        }
      }
      serviced = false;
      return serviced;
    } else {
      serviced = false;
      return serviced;
    }
  }

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"INTCNT")) {
    if (paramcount > 1) {
      uint8_t pin = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
      if (pin < MCP230xx1_pincount) {
        if (pin == 0) {
          if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "0")) validpin=true;
        } else {
          validpin = true;
        }
      }
      if (validpin) {
        if (paramcount > 2) {
          uint8_t intcnt = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
          if ((intcnt >= 0) && (intcnt <= 1)) {
            Settings.mcp230xx1_config[pin].int_count_en=intcnt;
            if (Settings.mcp230xx1_config[pin].int_report_defer) {
              Settings.mcp230xx1_config[pin].int_report_defer=0;
              AddLog_P2(LOG_LEVEL_INFO, PSTR("*** WARNING *** - Disabled INTDEF for pin D%i"),pin);
            }
            if (Settings.mcp230xx1_config[pin].int_report_mode < 3) {
              Settings.mcp230xx1_config[pin].int_report_mode=3;
              AddLog_P2(LOG_LEVEL_INFO, PSTR("*** WARNING *** - Disabled immediate interrupt/telemetry reporting for pin D%i"),pin);
            }
            if ((Settings.mcp230xx1_config[pin].int_count_en) && (!Settings.mcp230xx1_int_timer)) {
              AddLog_P2(LOG_LEVEL_INFO, PSTR("*** WARNING *** - INTCNT enabled for pin D%i but global INTTIMER is disabled!"),pin);
            }
            MCP230xx1_CheckForIntCounter(); // update register on whether or not we should be counting interrupts
            Response_P(MCP230xx1_INTCFG_RESPONSE,"CNT",pin,Settings.mcp230xx1_config[pin].int_count_en);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
            return serviced;
          } else {
            serviced=false;
            return serviced;
          }
        } else {
          Response_P(MCP230xx1_INTCFG_RESPONSE,"CNT",pin,Settings.mcp230xx1_config[pin].int_count_en);  // "{\"MCP230xx1_INT%s\":{\"D_%i\":%i}}";
          return serviced;
        }
      }
      serviced = false;
      return serviced;
    } else {
      serviced = false;
      return serviced;
    }
  }

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"INTRETAIN")) {
    if (paramcount > 1) {
      uint8_t pin = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
      if (pin < MCP230xx1_pincount) {
        if (pin == 0) {
          if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "0")) validpin=true;
        } else {
          validpin = true;
        }
      }
      if (validpin) {
        if (paramcount > 2) {
          uint8_t int_retain = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
          if ((int_retain >= 0) && (int_retain <= 1)) {
            Settings.mcp230xx1_config[pin].int_retain_flag=int_retain;
            Response_P(MCP230xx1_INTCFG_RESPONSE,"INT_RETAIN",pin,Settings.mcp230xx1_config[pin].int_retain_flag);
            MCP230xx1_CheckForIntRetainer();
            return serviced;
          } else {
            serviced=false;
            return serviced;
          }
        } else {
          Response_P(MCP230xx1_INTCFG_RESPONSE,"INT_RETAIN",pin,Settings.mcp230xx1_config[pin].int_retain_flag);
          return serviced;
        }
      }
      serviced = false;
      return serviced;
    } else {
      serviced = false;
      return serviced;
    }
  }

  uint8_t pin = atoi(subStr(sub_string, XdrvMailbox.data, ",", 1));

  if (pin < MCP230xx1_pincount) {
    if (0 == pin) {
      if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1), "0")) validpin=true;
    } else {
      validpin=true;
    }
  }
  if (validpin && (paramcount > 1)) {
    if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "?")) {
      uint8_t port = 0;
      if (pin > 7) { port = 1; }
      uint8_t portdata = MCP230xx1_readGPIO(port);
      char pulluptxtr[7],pinstatustxtr[7];
      char intmodetxt[9];
      sprintf(intmodetxt,IntModeTxt1(Settings.mcp230xx1_config[pin].int_report_mode));
      sprintf(pulluptxtr,ConvertNumTxt(Settings.mcp230xx1_config[pin].pullup));
#ifdef USE_MCP230xx1_OUTPUT
      uint8_t pinmod = Settings.mcp230xx1_config[pin].pinmode;
      sprintf(pinstatustxtr,ConvertNumTxt(portdata>>(pin-(port*8))&1,pinmod));
      Response_P(MCP230xx1_SENSOR_RESPONSE,pin,pinmod,pulluptxtr,intmodetxt,pinstatustxtr);
#else // not USE_MCP230xx1_OUTPUT
      sprintf(pinstatustxtr,ConvertNumTxt(portdata>>(pin-(port*8))&1));
      Response_P(MCP230xx1_SENSOR_RESPONSE,pin,Settings.mcp230xx1_config[pin].pinmode,pulluptxtr,intmodetxt,pinstatustxtr);
#endif //USE_MCP230xx1_OUTPUT
      return serviced;
    }
#ifdef USE_MCP230xx1_OUTPUT
    if (Settings.mcp230xx1_config[pin].pinmode >= 5) {
      uint8_t pincmd = Settings.mcp230xx1_config[pin].pinmode - 5;
      if ((!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "ON")) || (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "1"))) {
        MCP230xx1_SetOutPin(pin,abs(pincmd-1));
        return serviced;
      }
      if ((!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "OFF")) || (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "0"))) {
        MCP230xx1_SetOutPin(pin,pincmd);
        return serviced;
      }
      if ((!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "T")) || (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 2), "2")))  {
        MCP230xx1_SetOutPin(pin,2);
        return serviced;
      }
    }
#endif // USE_MCP230xx1_OUTPUT
    uint8_t pinmode = 0;
    uint8_t pullup = 0;
    uint8_t intmode = 0;
    if (paramcount > 1) {
      pinmode = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
    }
    if (paramcount > 2) {
      pullup = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
    }
    if (paramcount > 3) {
      intmode = atoi(subStr(sub_string, XdrvMailbox.data, ",", 4));
    }
#ifdef USE_MCP230xx1_OUTPUT
    if ((pin < MCP230xx1_pincount) && (pinmode > 0) && (pinmode < 7) && (pullup < 2) && (paramcount > 2)) {
#else // not use OUTPUT
    if ((pin < MCP230xx1_pincount) && (pinmode > 0) && (pinmode < 5) && (pullup < 2) && (paramcount > 2)) {
#endif // USE_MCP230xx1_OUTPUT
      Settings.mcp230xx1_config[pin].pinmode=pinmode;
      Settings.mcp230xx1_config[pin].pullup=pullup;
      if ((pinmode > 1) && (pinmode < 5)) {
        if ((intmode >= 0) && (intmode <= 3)) {
          Settings.mcp230xx1_config[pin].int_report_mode=intmode;
        }
      } else {
        Settings.mcp230xx1_config[pin].int_report_mode=3; // Int mode not valid for pinmodes other than 2 through 4
      }
      MCP230xx1_ApplySettings();
      uint8_t port = 0;
      if (pin > 7) { port = 1; }
      uint8_t portdata = MCP230xx1_readGPIO(port);
      char pulluptxtc[7], pinstatustxtc[7];
      char intmodetxt[9];
      sprintf(pulluptxtc,ConvertNumTxt(pullup));
      sprintf(intmodetxt,IntModeTxt1(Settings.mcp230xx1_config[pin].int_report_mode));
#ifdef USE_MCP230xx1_OUTPUT
      sprintf(pinstatustxtc,ConvertNumTxt(portdata>>(pin-(port*8))&1,Settings.mcp230xx1_config[pin].pinmode));
#else  // not USE_MCP230xx1_OUTPUT
      sprintf(pinstatustxtc,ConvertNumTxt(portdata>>(pin-(port*8))&1));
#endif // USE_MCP230xx1_OUTPUT
      Response_P(MCP230xx1_SENSOR_RESPONSE,pin,pinmode,pulluptxtc,intmodetxt,pinstatustxtc);
      return serviced;
    }
  } else {
    serviced=false; // no valid pin was used
    return serviced;
  }
  return serviced;
}

#ifdef USE_MCP230xx1_DISPLAYOUTPUT

const char HTTP_SNS_MCP230xx1_OUTPUT[] PROGMEM = "{s}MCP230XX D%d{m}%s{e}"; // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>

void MCP230xx1_UpdateWebData(void)
{
  uint8_t gpio1 = MCP230xx1_readGPIO(0);
  uint8_t gpio2 = 0;
  if (2 == MCP230xx1_type) {
    gpio2 = MCP230xx1_readGPIO(1);
  }
  uint16_t gpio = (gpio2 << 8) + gpio1;
  for (uint32_t pin = 0; pin < MCP230xx1_pincount; pin++) {
    if (Settings.mcp230xx1_config[pin].pinmode >= 5) {
      char stt[7];
      sprintf(stt,ConvertNumTxt((gpio>>pin)&1,Settings.mcp230xx1_config[pin].pinmode));
      WSContentSend_PD(HTTP_SNS_MCP230xx1_OUTPUT, pin, stt);
    }
  }
}

#endif // USE_MCP230xx1_DISPLAYOUTPUT

#ifdef USE_MCP230xx1_OUTPUT

void MCP230xx1_OutputTelemetry(void)
{
  uint8_t outputcount = 0;
  uint16_t gpiototal = 0;
  uint8_t gpioa = 0;
  uint8_t gpiob = 0;
  gpioa=MCP230xx1_readGPIO(0);
  if (2 == MCP230xx1_type) { gpiob=MCP230xx1_readGPIO(1); }
  gpiototal=((uint16_t)gpiob << 8) | gpioa;
  for (uint32_t pinx = 0;pinx < MCP230xx1_pincount;pinx++) {
    if (Settings.mcp230xx1_config[pinx].pinmode >= 5) outputcount++;
  }
  if (outputcount) {
    char stt[7];
    ResponseTime_P(PSTR(",\"MCP230_OUT\":{"));
    for (uint32_t pinx = 0;pinx < MCP230xx1_pincount;pinx++) {
      if (Settings.mcp230xx1_config[pinx].pinmode >= 5) {
        sprintf(stt,ConvertNumTxt(((gpiototal>>pinx)&1),Settings.mcp230xx1_config[pinx].pinmode));
        ResponseAppend_P(PSTR("\"OUT_D%i\":\"%s\","),pinx,stt);
      }
    }
    ResponseAppend_P(PSTR("\"END\":1}}"));
    MqttPublishTeleSensor();
  }
}

#endif // USE_MCP230xx1_OUTPUT

void MCP230xx1_Interrupt_Counter_Report(void) {
  ResponseTime_P(PSTR(",\"MCP230_INTTIMER\":{"));
  for (uint32_t pinx = 0;pinx < MCP230xx1_pincount;pinx++) {
    if (Settings.mcp230xx1_config[pinx].int_count_en) { // Counting is enabled for this pin so we add to report
      ResponseAppend_P(PSTR("\"INTCNT_D%i\":%i,"),pinx,MCP230xx1_int_counter[pinx]);
      MCP230xx1_int_counter[pinx]=0;
    }
  }
  ResponseAppend_P(PSTR("\"END\":1}}"));
  MqttPublishTeleSensor();
  MCP230xx1_int_sec_counter = 0;
}

void MCP230xx1_Interrupt_Retain_Report(void) {
  uint16_t retainresult = 0;
  ResponseTime_P(PSTR(",\"MCP_INTRETAIN\":{"));
  for (uint32_t pinx = 0;pinx < MCP230xx1_pincount;pinx++) {
    if (Settings.mcp230xx1_config[pinx].int_retain_flag) {
      ResponseAppend_P(PSTR("\"D%i\":%i,"),pinx,MCP230xx1_int_retainer[pinx]);
      retainresult |= (((MCP230xx1_int_retainer[pinx])&1) << pinx);
      MCP230xx1_int_retainer[pinx]=0;
    }
  }
  ResponseAppend_P(PSTR("\"Value\":%u}}"),retainresult);
  MqttPublishTeleSensor();
}

/*********************************************************************************************\
   Interface
\*********************************************************************************************/

bool Xsns68(uint8_t function)
{
  if (!I2cEnabled(XI2C_22)) { return false; }

  bool result = false;

  if (FUNC_INIT == function) {
      MCP230xx1_Detect();
  }
  else if (MCP230xx1_type) {
    switch (function) {
      case FUNC_EVERY_50_MSECOND:
        if (MCP230xx1_int_en) { // Only check for interrupts if its enabled on one of the pins
          MCP230xx1_int_prio_counter++;
          if ((MCP230xx1_int_prio_counter) >= (Settings.mcp230xx1_int_prio)) {
            MCP230xx1_CheckForInterrupt();
            MCP230xx1_int_prio_counter=0;
          }
        }
        break;
      case FUNC_EVERY_SECOND:
        if (MCP230xx1_int_counter_en) {
          MCP230xx1_int_sec_counter++;
          if (MCP230xx1_int_sec_counter >= Settings.mcp230xx1_int_timer) { // Interrupt counter interval reached, lets report
            MCP230xx1_Interrupt_Counter_Report();
          }
        }
        if (tele_period == 0) {
          if (MCP230xx1_int_retainer_en) { // We have pins configured for interrupt retain reporting
            MCP230xx1_Interrupt_Retain_Report();
          }
#ifdef USE_MCP230xx1_OUTPUT
          MCP230xx1_OutputTelemetry();
#endif // USE_MCP230xx1_OUTPUT
       
        }
        break;
      case FUNC_JSON_APPEND:
        MCP230xx1_Show(1);
        break;
      case FUNC_COMMAND_SENSOR:
        if (XSNS_68 == XdrvMailbox.index) {
          result = MCP230xx1_Command();
        }
        break;
#ifdef USE_WEBSERVER
#ifdef USE_MCP230xx1_OUTPUT
#ifdef USE_MCP230xx1_DISPLAYOUTPUT
      case FUNC_WEB_SENSOR:
        MCP230xx1_UpdateWebData();
        break;
#endif // USE_MCP230xx1_DISPLAYOUTPUT
#endif // USE_MCP230xx1_OUTPUT
#endif // USE_WEBSERVER
    }
  }
  return result;
}

#endif  // USE_MCP230xx1
#endif  // USE_I2C

