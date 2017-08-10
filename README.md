# ipmi-fru-it
[![Github Issues](https://img.shields.io/github/issues/mandeepsandhu/ipmi-fru-it.svg)](https://github.com/mandeepsandhu/ipmi-fru-it/issues)
[![Average time to resolve an issue](http://isitmaintained.com/badge/resolution/mandeepsandhu/ipmi-fru-it.svg)](http://isitmaintained.com/project/mandeepsandhu/ipmi-fru-it
"Average time to resolve an issue")

*`ipmi-fru-it`* (pronounced ipmi-_fruit_) is a command-line utility for reading and writing IPMI FRU data.

It follows the [IPMI Management FRU Information Storage Definition Specification](http://www.intel.com/content/dam/www/public/us/en/documents/product-briefs/platform-management-fru-document-rev-1-2-feb-2013.pdf) when reading/writing a FRU data file.

**NOTE:** This tool uses the `iniparser` library for parsing the config file. This library was modified for some bug-fixes.

## Usage:
Generating a FRU data file:
```
$ ipmi-fru-it -w -s 2048 -c fru.conf -o FRU.bin
```
Reading a FRU data file:
```
$ ipmi-fru-it -r -i FRU.bin
```
## FRU config
The FRU data to be written is provided by means of a _config file_ as input to `ipmi-fru-it`. The config file follows a simple **INI** file format and provides data for the various FRU sections. 

`ipmi-fru-it` understands **only** the following INI section headers (other sections are simply ignored):
* `iua`
* `cia`
* `bia`
* `pia`

These map directly to the various FRU sections of the **FRU Information Storage Definition** specifications. **ALL** sections are optional and can have additional custom keys, which are placed in the custom area of that section (see FRU storage def specs). Each **pre-defined field** not specified in a section, is stored as an _empty type/length_ value.

### Section headers
1. `iua` (**Internal Use Area**): If this section is specified, it MUST have a key - `bin_file` with a value as the absolute path of a file that you want included in the internal use area. The file is treated as a binary file and it's contents are copied _as-is_ into this FRU section.
2. `cia` (**Chassis Info Area**): If this section is specified, it _should_ have the following pre-defined keys:
  1. `chassis_type` - A single byte number specifying the type of chassis as defined in [SMBIOS Reference Spec](http://www.dmtf.org/sites/default/files/standards/documents/DSP0134_2.7.1.pdf), (7.4.1 System Enclosure or Chassis Types).
  2. `part_number` - ASCII string.
  3. `serial_number` - ASCII string.
3. `bia` (**Board Info Area**): If this section is specified, it _should_ have the following pre-defined keys:
  1. `mfg_datetime` - Number of minutes from 0:00 hrs 1/1/96.
  2. `manufacturer` - ASCII string.
  3. `product_name` - ASCII string.
  4. `serial_number` - ASCII string.
  5. `part_number`  - ASCII string.
4. `pia` (**Platform Info Area**): If this section is specified, it _should_ have the following pre-defined keys:
  1. `manufacturer` - ASCII string.
  2. `product_name` - ASCII string.
  3. `part_number`  - ASCII string.
  4. `version`  - ASCII string.
  5. `serial_number` - ASCII string.
  6. `asset_tag` - ASCII string.

## Reading FRU data file
TBD.

## Known Issues
* Any ASCII _value_ in the config file MUST be in UPPER case.
* Currently, there's no way to specify **binary or BCD data** for any of the FRU sections (`ipmi-fru-it` assumes all keys are ASCII text).

## TODO
* Implement `-r` option - read contents of a FRU data file.
* Support for MultiRecord Headers.
* Support for FRU File ID.
* ASCII values should be case-insensitive.
* Find a way to specify BCD values.
