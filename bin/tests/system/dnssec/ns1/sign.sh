#!/bin/sh
#
# Copyright (C) 2000  Internet Software Consortium.
# 
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
# 
# THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
# ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
# CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
# DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
# PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
# ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
# SOFTWARE.


zone=.
infile=root.db.in
zonefile=root.db

keyname=`$KEYGEN -a RSA -b 768 -n zone $zone`

(cd ../ns2 && sh sign.sh )

cp ../ns2/example.keyset .

$KEYSIGNER example.keyset $keyname

cat example.signedkey >> ../ns2/example.db.signed

$KEYSETTOOL -t 3600 $keyname

cat $infile $keyname.key > $zonefile

$SIGNER -o $zone $zonefile

# Configure the resolving server with a trusted key.

cat $keyname.key | perl -n -e '
my ($dn, $class, $type, $flags, $proto, $alg, @rest) = split;
my $key = join("", @rest);
print <<EOF
trusted-keys {
    "$dn" $flags $proto $alg "$key";
};
EOF
' > trusted.conf
cp trusted.conf ../ns2/trusted.conf
cp trusted.conf ../ns3/trusted.conf
cp trusted.conf ../ns4/trusted.conf