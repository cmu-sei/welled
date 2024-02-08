#!/bin/bash

VERSION=$1

RELEASEDATE=`date -u +%FT%T.000000%:z`
TIMESTAMP=`date -u +%FT%T.000000`
DATE=`date +%s`

echo Building: wmasterd stage.vtar

rm -rf ../tmp
mkdir -p ../tmp/usr/lib/vmware/wmasterd/bin/
cp ../dist/esx/wmasterd ../tmp/usr/lib/vmware/wmasterd/bin/wmasterd
chmod 775 ../tmp/usr/lib/vmware/wmasterd/bin/wmasterd
cp -r ../etc ../tmp/
cd ../tmp/; tar --owner=0 --group=0 -cvf stage.vtar usr/ etc/; cd -
rm -rf ../tmp/etc/ ../tmp/usr/

echo Building: CMU_bootbank_wmasterd_$VERSION.$DATE.vib

SHA1=`sha1sum ../tmp/stage.vtar| cut -f 1 -d ' '`

cd ../tmp/; gzip -c stage.vtar > wmasterd; cd -

SIZE=`stat --format %s ../tmp/wmasterd`
SHA256=`sha256sum ../tmp/wmasterd | cut -f 1 -d ' '`

# create empty signature file
touch ../tmp/sig.pkcs7

# update descriptor file for inside the vib
cp ../VIB-DATA/descriptor.xml ../tmp/descriptor.xml
sed -i "s/VERSION/$VERSION/g" ../tmp/descriptor.xml
sed -i "s/VERSION/$VERSION/g" ../tmp/descriptor.xml
sed -i "s/SIZE/$SIZE/" ../tmp/descriptor.xml
sed -i "s/SHA256/$SHA256/" ../tmp/descriptor.xml
sed -i "s/SHA1/$SHA1/" ../tmp/descriptor.xml
sed -i "s/RELEASEDATE/$RELEASEDATE/" ../tmp/descriptor.xml
cd ../tmp; ar r CMU_bootbank_wmasterd_$VERSION.$DATE.vib descriptor.xml sig.pkcs7 wmasterd; cd -
mv ../tmp/CMU_bootbank_wmasterd_$VERSION.$DATE.vib ../dist/esx/
rm ../tmp/sig.pkcs7 ../tmp/stage.vtar ../tmp/wmasterd
rm -rf ../tmp

echo Building: CMU-wmasterd-$VERSION-$DATE.offline_bundle.zip

mkdir -p ../tmp/vib20/wmasterd/
cp ../dist/esx/CMU_bootbank_wmasterd_$VERSION.$DATE.vib ../tmp/vib20/wmasterd/

VIBSIZE=`stat --format %s ../tmp/vib20/wmasterd/CMU_bootbank_wmasterd_$VERSION.$DATE.vib`
VIBSHA256=`sha256sum ../tmp/vib20/wmasterd/CMU_bootbank_wmasterd_$VERSION.$DATE.vib | cut -f 1 -d ' '`

cp ../BUNDLE-DATA/*xml ../tmp/
cp -pr ../BUNDLE-DATA/metadata/ ../tmp/
cp ../tmp/vendor-index.xml ../tmp/metadata/

# update vmware xml
sed -i "s/TIMESTAMP/$TIMESTAMP/" ../tmp/metadata/vmware.xml
sed -i "s/RELEASEDATE/$RELEASEDATE/" ../tmp/metadata/vmware.xml
sed -i "s/VERSION/$VERSION/g" ../tmp/metadata/vmware.xml
sed -i "s/DATE/$DATE/g" ../tmp/metadata/vmware.xml
sed -i "s/VIBSIZE/$VIBSIZE/g" ../tmp/metadata/vmware.xml
sed -i "s/VIBSHA256/$VIBSHA256/g" ../tmp/metadata/vmware.xml

# update bulletin
mv ../tmp/metadata/bulletins/wmasterd.xml ../tmp/metadata/bulletins/wmasterd-$VERSION.$DATE.xml
sed -i "s/VERSION/$VERSION/g" ../tmp/metadata/bulletins/wmasterd-$VERSION.$DATE.xml
sed -i "s/RELEASEDATE/$RELEASEDATE/" ../tmp/metadata/bulletins/wmasterd-$VERSION.$DATE.xml
sed -i "s/DATE/$DATE/" ../tmp/metadata/bulletins/wmasterd-$VERSION.$DATE.xml

# update wmasterd xml
mv ../tmp/metadata/vibs/wmasterd.xml ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/RELEASEDATE/$RELEASEDATE/" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/VERSION/$VERSION/g" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/DATE/$DATE/g" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/VIBSIZE/$VIBSIZE/g" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/VIBSHA256/$VIBSHA256/g" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/SIZE/$SIZE/" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/SHA256/$SHA256/" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml
sed -i "s/SHA1/$SHA1/" ../tmp/metadata/vibs/wmasterd-$VERSION-$DATE.xml

# build metadata.zip
cd ../tmp/metadata; zip -r ../metadata.zip vmware.xml vendor-index.xml bulletins/wmasterd-$VERSION.$DATE.xml vibs/wmasterd-$VERSION-$DATE.xml; cd -
rm -rf ../tmp/metadata/

# build offine_bundle zip
cd ../tmp/; zip -r ../dist/esx/CMU-wmasterd-$VERSION-$DATE.offline_bundle.zip index.xml vendor-index.xml metadata.zip vib20/; cd -
rm -rf ../tmp



