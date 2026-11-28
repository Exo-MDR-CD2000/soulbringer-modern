#!/bin/sh
# Build and package release/SoulbringerModern-<version>.zip
# Zip layout (extract into the game folder, then run install.bat):
#   install.bat, uninstall.bat, SoulbringerModern.ini, README.txt
#   SoulbringerModern/D3d7Lib.dll   <- kept in a subfolder so extracting never
#                                      overwrites the game's original renderer
#   SoulbringerModern/fonts/        Cardo-Bold.ttf, Montserrat-ExtraBold.ttf, SoulbringerUncial-Regular.ttf (Eagle Lake, lining figures), MedievalSharp-Regular.ttf, SoulbringerText-Regular.ttf (names, tooltips, journal), SoulbringerCredits-Regular.ttf (credits, GAME OVER), with their OFL texts (HD fonts)
set -e
cd "$(dirname "$0")"
sh ./build.sh
VER=$(sed -n 's/^#define SBM_VERSION "\(.*\)"/\1/p' src/soulbringer_modern.c)
OUT=release/SoulbringerModern-$VER
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/SoulbringerModern"
cp build/D3d7Lib.dll "$OUT/SoulbringerModern/"
mkdir -p "$OUT/SoulbringerModern/fonts"
cp dist/fonts/Cardo-Bold.ttf dist/fonts/Cardo-OFL.txt dist/fonts/Montserrat-ExtraBold.ttf dist/fonts/Montserrat-OFL.txt \
   dist/fonts/SoulbringerUncial-Regular.ttf dist/fonts/SoulbringerUncial-OFL.txt dist/fonts/MedievalSharp-Regular.ttf dist/fonts/MedievalSharp-OFL.txt dist/fonts/SoulbringerText-Regular.ttf dist/fonts/SoulbringerText-OFL.txt dist/fonts/SoulbringerCredits-Regular.ttf dist/fonts/SoulbringerCredits-OFL.txt "$OUT/SoulbringerModern/fonts/"   # HD fonts (SIL OFL: licence travels with the font)
cp dist/install.bat dist/uninstall.bat dist/SoulbringerModern.ini "$OUT/"
sed 's/\r*$/\r/' dist/README.txt > "$OUT/README.txt"   # CRLF once, whether the checkout has LF or CRLF
(cd "$OUT" && "${PYTHON:-python3}" -c "import zipfile,os;z=zipfile.ZipFile('../SoulbringerModern-$VER.zip','w',zipfile.ZIP_DEFLATED);[z.write(os.path.join(r,f),os.path.relpath(os.path.join(r,f),'.')) for r,_,fs in os.walk('.') for f in fs];z.close()")
echo "packaged release/SoulbringerModern-$VER.zip"
