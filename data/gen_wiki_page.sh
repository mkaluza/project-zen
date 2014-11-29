#!/bin/bash
src=$1
datadir=$src/data
imgdir=$src/images

set -x

#use hotlink for forums (1)
if [ -f $datadir/images.postimage ]; then
	grep -v -e "^$" $datadir/images.postimage | sed -e "s/^.*img\]\(.*\)\[.img.*$/\1/g" > $datadir/images.url
fi
cmd="./jj2.py "
for n in `cat $datadir/img_names.txt`; do
	url=`grep "$n" $datadir/images.url | head -n 1`
	if [ $? -gt 0 ]; then
		echo "Image $n not found!"
		continue
	fi
	eval ${n}=\"$url\"
	cmd="$cmd --var $n=\"$url\""
done
$cmd wiki_images.md.tpl > $imgdir/wiki_images.md

./jj2.py --var q1_results=$datadir/q1.dat --var q2_results=$datadir/q2.dat --var images=$imgdir/wiki_images.md wiki.report.md.tpl > $datadir/wiki.md
