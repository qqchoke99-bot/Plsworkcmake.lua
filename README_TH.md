# Sound Physics Lite — Raycast build

รุ่นนี้เพิ่ม **block line-of-sight raycast** ให้กับ source จริง โดยใช้ `Player::canSee()` ของ LeviLamina เมื่อ client API headers ของเวอร์ชันที่ตรงกันมีให้กับ build

## Logic

1. Hook `0x110CB1D0`
2. หลัง `playSound` อ่าน `Channel*` จาก `object + 0x78`
3. อ่านตำแหน่งเสียงจาก `object + 0x98` (Vec3 ที่ถูกส่งเข้า `set3DAttributes`)
4. หา local player จาก Levi client service
5. ใช้ `Player::canSee(sourcePosition, ShapeType(0))` เป็น native block raycast
6. ถ้ามีกำแพงบัง → ลด LowPassGain เป็น `occluded_lowpass_gain`
7. ถ้าไม่มีสิ่งกีดขวาง → คืน gain เป็น 1.0

## สำคัญ

`preloader-android 0.2.2` อย่างเดียวมี public SDK surface สำหรับ `pl/*` แต่ไม่ได้ให้ LeviLamina client API headers ทั้งหมด ดังนั้น GitHub Actions ที่ไม่มี `third_party/levi-client-sdk` จะ build ได้ แต่จะปิด raycast path อัตโนมัติ

เพื่อให้ raycast ทำงานจริง ต้องวาง client SDK ที่ **ตรงกับ Minecraft/Levi รุ่นที่ใช้** ไว้ที่:

```text
third_party/levi-client-sdk/include/
├── ll/api/service/TargetedBedrock.h
└── mc/world/actor/player/Player.h
```

อย่าเอา header จาก Minecraft คนละเวอร์ชันมาแทน เพราะ native ABI ต้องตรงกัน

## Package

GitHub Actions จะสร้าง:

```text
SPL.levipack
└── SPL.levipack/
    ├── manifest.json
    ├── libSoundPhysicsLite.so
    └── icon.png
```

## หมายเหตุ

Reverb ยังไม่ได้เพิ่มในรุ่นนี้; raycast นี้มีหน้าที่ตรวจว่ามีกำแพงระหว่าง listener กับ source แล้วทำให้เสียงถูก low-pass มากขึ้น
