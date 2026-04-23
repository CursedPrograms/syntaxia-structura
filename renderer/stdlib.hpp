#pragma once
// Embedded standard library .syn files.
// Checked before filesystem on every <@>import</@>.
// Add new library files here; they ship inside SynReader.exe.

#include <map>
#include <string>

static const std::map<std::string, std::string> STDLIB = {

{"math_utils.syn", R"SYN(
<structura version="1.0" stdlib="true">
<func name="math_utils">
    <var name="PI"    type="float">3.14159265358979</var>
    <var name="E"     type="float">2.71828182845905</var>
    <var name="PHI"   type="float">1.61803398874989</var>
    <var name="SQRT2" type="float">1.41421356237310</var>
    <var name="SQRT3" type="float">1.73205080756888</var>
    <var name="LN2"   type="float">0.69314718055995</var>
    <var name="LN10"  type="float">2.30258509299405</var>
    <var name="G"     type="float">9.80665</var>
    <var name="C"     type="float">299792458.0</var>
    <var name="RHO_WATER" type="float">1000.0</var>
    <var name="RHO_SEA"   type="float">1025.0</var>
</func>
</structura>
)SYN"},

{"units.syn", R"SYN(
<structura version="1.0" stdlib="true">
<func name="units">
    <var name="KG_TO_LB"  type="float">2.20462262185</var>
    <var name="KM_TO_MI"  type="float">0.621371192237</var>
    <var name="C_TO_F_MUL" type="float">1.8</var>
    <var name="C_TO_F_ADD" type="float">32.0</var>
    <var name="M_TO_FT"   type="float">3.28083989501</var>
    <var name="L_TO_GAL"  type="float">0.264172052358</var>
</func>
</structura>
)SYN"},

{"finance.syn", R"SYN(
<structura version="1.0" stdlib="true">
<func name="finance">
    <var name="TAX_STANDARD" type="float">0.20</var>
    <var name="TAX_REDUCED"  type="float">0.05</var>
    <var name="TAX_ZERO"     type="float">0.00</var>
</func>
</structura>
)SYN"},

};
