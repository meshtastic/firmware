unzip firmware.zip -d firmware
cp uf2conv.py firmware/
cp uf2families.json firmware/
cd firmware/
python3 uf2conv.py firmware.bin -c -b 0x26000 -f 0xADA52840
mv flash.uf2 ../
cd ..
rm -rf firmware


