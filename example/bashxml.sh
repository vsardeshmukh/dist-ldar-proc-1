#!/bin/bash
#rdom () { local IFS=\> ; read -d \< E C ;}
echo $1 $2
read_dom () {
    local IFS=\>
    read -d \< ENTITY CONTENT
    local RET=$?
    TAG_NAME=${ENTITY%% *}
    ATTRIBUTES=${ENTITY#* }
    return $RET
}
parse_dom () {
    if [[ $TAG_NAME = "coords" ]] ; then
        eval local $ATTRIBUTES
        echo "$x $y"
    fi
}
while read_dom; do
    parse_dom
done < $1 >$2
# $Id: $


