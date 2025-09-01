# Secure Wireless Authentication Network (SWAN) Electrical & Computer Engineering Class of 2025

## Overview
**SWAN (Secure Wireless Authentication Network)** is a senior capstone project focused on developing a safer, more seamless alternative to traditional RFID and NFC-based access systems.  

Current wireless access solutions, such as student ID cards or key fobs, suffer from two major issues:
1. **Security vulnerabilities** – Devices such as the Flipper Zero can easily clone, replay, or brute-force RFID/NFC signals, compromising access control systems.  
2. **Limited usability** – Cards must be presented within a few centimeters of a scanner, which is inconvenient when carrying items (e.g., food, books, bags) and detracts from user experience.

SWAN leverages **Ultra-Wideband (UWB) technology** to deliver both stronger security and automatic, hands-free authentication.  

---

## Problem Statement
Existing RFID and NFC access systems are vulnerable and inconvenient:
- **Vulnerabilities**: RFID/NFC cards transmit predictable signals that can be intercepted and cloned.  
- **User experience**: Scanning requires close proximity, forcing users to stop what they’re doing to present their ID.  

This is especially problematic in settings like dormitories or offices where secure yet convenient access is critical.

---

## Solution
SWAN provides an **automatic, secure, and user-friendly wireless authentication system** powered by **UWB**.  

Key design features:
- **Hardware**:  
  - Custom-designed PCBs  
  - **ESP32-C3 WROOM** microcontroller  
  - **Decawave DWM3000 UWB transceiver**  
- **Architecture**:  
  - A distributed network of devices with a **home anchor** that verifies access  
  - Automatic authentication within a defined UWB range (no card-swipe required)  
- **Security**:  
  - Communication encrypted using **AES-128**, the standard for low-power embedded devices  
  - Resistant to replay and cloning attacks compared to RFID/NFC
  - Back method to lost tags which includes an optical finger print reader, independent from the anchor removing depedency.
 
### What this enables
The idea is that these `tag` devices can take space in your wallet as a card, and something on your keychain.
These are items that you typically always have on your person, and want to keep import track of. In the unfortunate event 
that one of these devices are stolen, rest assured you're provided with a back biometric feature and that **two** tags 
must be present at all times.

---

## Benefits
- **Improved security**: Protects against Flipper Zero-style exploits and unauthorized duplication.  
- **Hands-free access**: Users can enter secured areas automatically, without swiping or tapping a card.  
- **Energy-efficient**: Designed with low-power embedded constraints in mind.  
- **Scalable**: Can be deployed in dorms, offices, or any facility requiring secure access.  

---

## Technology Stack
- **Embedded Hardware**: ESP32-C3 WROOM, DWM3000 UWB  
- **Firmware**: C/C++ with ESP-IDF and Arduino frameworks  
- **Networking**: UWB communication protocol, AES-128 encryption  
- **Custom PCBs**: Designed for modularity and integration with door access hardware

---

# Pictures

## Tag PCB v3
<img width="597" height="438" alt="image" src="https://github.com/user-attachments/assets/42ec3647-0fd7-474e-b2d9-3dfd2a6edf4b" />

## Home Anchor PCB v1
<img width="834" height="384" alt="image" src="https://github.com/user-attachments/assets/b30a2dc9-13c4-4ea9-8b46-50f87fe7a93d" />





