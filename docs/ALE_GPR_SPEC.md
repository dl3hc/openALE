# ALE-GPR v1.1 — Implementation Specification

## 1. Purpose

Implement support for the **Open Standard ALE-GPR (Geo Position Report)** format transported as an ALE AMD message.

ALE-GPR encodes a geographic position as a human-readable ASCII string. The format is intended to work on ALE equipment with limited character sets and display capabilities.

The implementation must support:

* Parsing incoming ALE-GPR AMD messages.
* Validating the individual fields.
* Converting valid coordinates into numeric latitude/longitude values.
* Generating outgoing ALE-GPR messages.
* Preserving the raw field values where appropriate.
* Handling manually entered or invalid GPS data marked with `#`.

---

# 2. Wire Format

An ALE-GPR message consists of exactly 7 fields:

```text
GPR*OBJECT*LATITUDE*LONGITUDE*ALTITUDE*DATE/TIME*COMMENT
```

Example:

```text
GPR*KQ6XA*37N654321*122W987654*000003M*20050821Z135235*EVERYTHING FINE
```

There must be:

* No `*` before `GPR`.
* Exactly one `*` delimiter between fields.
* No trailing `*`.
* `GPR` must be the first field.

---

# 3. Character Set

For maximum compatibility, generated ALE-GPR messages should only use:

```text
ABCDEFGHIJKLMNOPQRSTUVWXYZ
0123456789
SPACE
*
#
```

Do not generate other punctuation characters unless explicitly required by a compatibility mode.

Important:

* `*` is reserved exclusively as the field delimiter.
* `#` may be used as a placeholder, null value, or manual/invalid GPS indicator.
* In latitude, longitude, altitude, or time fields, the presence of `#` indicates manually entered or non-valid GPS data.

---

# 4. Field Definitions

## Field 1 — GPR

```text
GPR
```

Length:

```text
Exactly 3 characters
```

Purpose:

```text
Identifies the AMD payload as an ALE Geo Position Report.
```

Validation:

```text
field[0] == "GPR"
```

---

## Field 2 — OBJECT

Format:

```text
OBJECT
```

Length:

```text
3 to 15 characters
```

Purpose:

```text
Identifies the object whose position is being reported.

Normally this is the sender's callsign or ALE address.
It may also identify another station or object being reported or relayed.
```

Allowed characters:

```text
A-Z
0-9
SPACE
#
```

Example:

```text
KQ6XA
```

Example:

```text
ABCDE0123456789
```

`#` may be used where a callsign would normally contain `/`.

Example conceptual mapping:

```text
CALL#SIGN
```

may represent:

```text
CALL/SIGN
```

The parser should preserve `#` exactly as transmitted unless an application-level display conversion is explicitly desired.

---

# 5. Field 3 — LATITUDE

Maximum length:

```text
9 characters
```

Format:

```text
DDHdddddd
```

Where:

```text
DD        = latitude degrees
H         = N or S
dddddd    = fractional decimal degrees
```

Example:

```text
37N654321
```

Meaning:

```text
37.654321° North
```

Equivalent numeric value:

```text
+37.654321
```

South example:

```text
12S345678
```

Meaning:

```text
-12.345678
```

Parsing rule:

```text
Latitude = degrees + fractional_part
```

The direction character replaces the normal decimal point.

Conceptually:

```text
37N654321
```

becomes:

```text
37.654321 N
```

Validation:

```text
degrees: 00–90
hemisphere: N or S
```

`#` handling:

If `#` occurs anywhere in the latitude field:

```text
valid_gps_position = false
manual_or_invalid_position = true
```

Do not reject the complete GPR message solely because of `#`.

Instead, preserve the raw value and mark the coordinate as unavailable or invalid.

---

# 6. Field 4 — LONGITUDE

Maximum length:

```text
10 characters
```

Format:

```text
DDDHdddddd
```

Where:

```text
DDD       = longitude degrees
H         = E or W
dddddd    = fractional decimal degrees
```

Example:

```text
122W987654
```

Meaning:

```text
122.987654° West
```

Equivalent numeric value:

```text
-122.987654
```

East example:

```text
008E123456
```

Meaning:

```text
+8.123456
```

Validation:

```text
degrees: 000–180
hemisphere: E or W
```

`#` handling:

If `#` occurs anywhere in the longitude field:

```text
valid_gps_position = false
manual_or_invalid_position = true
```

Preserve the raw value.

---

# 7. Field 5 — ALTITUDE

Maximum length:

```text
7 characters
```

Format:

```text
NUMBER + UNIT
```

Unit:

```text
M = meters
F = feet
```

Examples:

```text
000003M
3M
1234F
```

Meaning:

```text
3 meters
1234 feet
```

Minimum valid structure:

```text
1 or more digits followed by M or F
```

`#` handling:

If `#` occurs anywhere in the field:

```text
valid_gps_position = false
manual_or_invalid_position = true
```

The parser should preserve the raw field.

---

# 8. Field 6 — TIME

Fixed length:

```text
15 characters
```

Format:

```text
YYYYMMDDZhhmmss
```

Structure:

```text
YYYY = year
MM   = month
DD   = day
Z    = timezone separator
hh   = hour
mm   = minute
ss   = second
```

Example:

```text
20050821Z135235
```

Meaning:

```text
2005-08-21 13:52:35 UTC
```

The timezone separator is an uppercase letter.

Common values:

```text
Z = UTC / Zulu
J = local time
```

Validation:

```text
Year: 4 digits
Month: 01–12
Day: valid for month/year
Hour: 00–23
Minute: 00–59
Second: 00–59
Timezone separator: A-Z
```

`#` handling:

If `#` occurs anywhere in the time field:

```text
valid_timestamp = false
manual_or_invalid_position = true
```

Preserve the raw value.

---

# 9. Field 7 — COMMENT

Maximum length:

```text
25 characters
```

Spaces are allowed.

Example:

```text
EVERYTHING FINE HERE NOW
```

The comment may also contain implementation-specific or undefined information.

The parser should preserve the comment as transmitted.

The generator should truncate or reject comments longer than 25 characters.

Recommended behavior:

```text
If comment.length > 25:
    truncate to 25 characters
```

or, in strict mode:

```text
return validation error
```

---

# 10. Parsing Algorithm

Input:

```text
amd_text
```

Algorithm:

```text
1. Split amd_text on "*".

2. Require exactly 7 fields.

3. Validate:

   fields[0] == "GPR"

4. Assign:

   prefix    = fields[0]
   object    = fields[1]
   latitude  = fields[2]
   longitude = fields[3]
   altitude  = fields[4]
   time      = fields[5]
   comment   = fields[6]

5. Validate field lengths and allowed characters.

6. Detect "#":

   If "#" occurs in latitude, longitude, altitude, or time:

       manual_or_invalid_position = true

7. Attempt numeric parsing of:

   latitude
   longitude
   altitude
   timestamp

8. Parsing failures in optional or "#" marked fields must not necessarily reject the complete GPR.

9. Return both:

   - raw fields
   - parsed/normalized values
   - validation status
```

---

# 11. Recommended Data Model

```text
ALEGPR {
    raw: string

    object: string

    latitude_raw: string
    longitude_raw: string
    altitude_raw: string
    time_raw: string

    latitude: number | null
    longitude: number | null

    altitude: number | null
    altitude_unit: "M" | "F" | null

    timestamp: datetime | null
    timezone_code: string | null

    comment: string

    valid: boolean

    valid_position: boolean
    valid_timestamp: boolean

    manual_or_invalid_position: boolean

    errors: string[]
}
```

---

# 12. Coordinate Conversion

## Latitude

Example:

```text
37N654321
```

Convert to:

```text
37.654321
```

Pseudo-code:

```text
match:
    ^([0-9]{2})([NS])([0-9]+)$

degrees = integer(group1)
fraction = decimal("0." + group3)

value = degrees + fraction

if group2 == "S":
    value = -value
```

Maximum valid range:

```text
-90.0 <= latitude <= +90.0
```

---

## Longitude

Example:

```text
122W987654
```

Convert to:

```text
-122.987654
```

Pseudo-code:

```text
match:
    ^([0-9]{3})([EW])([0-9]+)$

degrees = integer(group1)
fraction = decimal("0." + group3)

value = degrees + fraction

if group2 == "W":
    value = -value
```

Maximum valid range:

```text
-180.0 <= longitude <= +180.0
```

---

# 13. Outgoing Message Generation

The generator should accept structured position data and produce:

```text
GPR*OBJECT*LATITUDE*LONGITUDE*ALTITUDE*TIME*COMMENT
```

Example input:

```text
object = "KQ6XA"

latitude = 37.654321
longitude = -122.987654

altitude = 3
altitude_unit = "M"

timestamp = 2005-08-21T13:52:35Z

comment = "EVERYTHING FINE"
```

Expected output:

```text
GPR*KQ6XA*37N654321*122W987654*000003M*20050821Z135235*EVERYTHING FINE
```

---

# 14. Generation Rules

## Latitude

Input:

```text
37.654321
```

Output:

```text
37N654321
```

Rules:

```text
positive = N
negative = S
```

The decimal point is replaced by the hemisphere character.

---

## Longitude

Input:

```text
-122.987654
```

Output:

```text
122W987654
```

Rules:

```text
positive = E
negative = W
```

The decimal point is replaced by the hemisphere character.

Longitude degrees should normally be zero-padded to three digits when necessary.

Example:

```text
8.123456
```

becomes:

```text
008E123456
```

---

## Altitude

Example:

```text
3 meters
```

Canonical output:

```text
000003M
```

Alternative valid compact form:

```text
3M
```

Recommended generator behavior:

```text
Use zero-padded canonical form where compatibility permits.
```

---

# 15. Error Handling

The parser must distinguish between:

```text
INVALID MESSAGE
```

and:

```text
VALID GPR WITH UNAVAILABLE/MANUAL DATA
```

Example invalid message:

```text
HELLO*WORLD
```

Reason:

```text
Not 7 fields
No GPR prefix
```

Example partially valid message:

```text
GPR*KQ6XA*##*###*#M*################*
```

This should not necessarily be discarded.

Instead:

```text
valid_gpr_structure = true
valid_position = false
manual_or_invalid_position = true
```

The raw message should remain available for display and logging.

---

# 16. Strict Validation vs Compatibility Mode

Implement two validation modes.

## Strict Mode

Reject messages that violate:

```text
- incorrect field count
- missing GPR prefix
- invalid required field lengths
- invalid coordinate ranges
- invalid timestamp structure
- invalid characters
```

## Compatibility Mode

Accept structurally recognizable GPR messages where possible.

Examples:

```text
- "#" placeholders
- manually entered positions
- unavailable GPS values
- compact altitude
- partially populated optional data
```

Compatibility mode should return warnings rather than rejecting the entire message.

---

# 17. Integration with ALE / AMD

ALE-GPR is transported as an AMD payload.

The AMD payload itself is:

```text
GPR*OBJECT*LATITUDE*LONGITUDE*ALTITUDE*TIME*COMMENT
```

The application should detect ALE-GPR by checking:

```text
payload.startsWith("GPR*")
```

or, more strictly:

```text
payload.split("*")[0] == "GPR"
```

Once detected:

```text
1. Parse as ALE-GPR.
2. Validate.
3. Store/display the structured position.
4. Preserve the original AMD text.
```

Non-GPR AMD messages must continue through the normal AMD handling path unchanged.

---

# 18. Acceptance Tests

The implementation must correctly parse:

```text
GPR*KQ6XA*37N654321*122W987654*000003M*20050821Z135235*EVERYTHING FINE
```

Expected:

```text
object = KQ6XA

latitude = +37.654321
longitude = -122.987654

altitude = 3
altitude_unit = M

timestamp = 2005-08-21T13:52:35Z

comment = EVERYTHING FINE
```

Test southern/eastern coordinates:

```text
GPR*TEST*12S345678*008E123456*150M*20260818Z120000*TEST POSITION
```

Expected:

```text
latitude = -12.345678
longitude = +8.123456
altitude = 150 meters
```

Test manually entered/invalid GPS data:

```text
GPR*TEST*##*###*#M*###############*MANUAL POSITION
```

Expected:

```text
GPR structure recognized = true
valid_position = false
manual_or_invalid_position = true
```

Test invalid prefix:

```text
XYZ*TEST*37N654321*122W987654*3M*20260818Z120000*INVALID
```

Expected:

```text
Not ALE-GPR
```

Test missing fields:

```text
GPR*TEST*37N654321
```

Expected:

```text
Invalid ALE-GPR structure
```

---

# 19. Implementation Priority

Implement in this order:

```text
1. GPR detection in incoming AMD messages.
2. Basic 7-field parser.
3. Latitude/longitude conversion.
4. # placeholder and invalid/manual data handling.
5. Altitude parsing.
6. Timestamp parsing.
7. Structured data model.
8. Outgoing GPR generator.
9. Strict/compatibility validation modes.
10. UI integration and position display.
```

The primary requirement is interoperability: valid standard ALE-GPR messages must parse correctly, while manually entered or partially invalid position data should be preserved and represented as such rather than unnecessarily discarded.
