# Renders a sheet of all of Palang's UI icons (dark 32 px, dark 16 px,
# light 16 px) to check them side by side. Needs inkscape and ImageMagick.
# Run it in a scratch folder holding copies of data/icons/scalable/actions/:
#   cp data/icons/scalable/actions/*.svg design/icons/render-sheet.sh /tmp/x/
#   cd /tmp/x && bash render-sheet.sh
mkdir -p r
row() { # bg fg accent px out names...
  bg=$1 fg=$2 ac=$3 px=$4 out=$5; shift 5; files=""
  for n in "$@"; do
    sed "s/#2e3436/$fg/; s/#f5b041/$ac/; s/#e01b24/#ff6b6b/" palang-$n-symbolic.svg > r/s.svg
    inkscape r/s.svg -o r/a.png -w $px 2>/dev/null
    convert -size 52x44 xc:"$bg" r/a.png -gravity center -composite +repage r/$n.png
    files="$files r/$n.png"
  done
  convert $files +append +repage $out
}
A="terminal search sidebar tab-new menu close go-up go-down replace reveal"
B="error warning info folder folder-open file file-code file-text file-image leopard"
row '#242424' '#eeeeec' '#f5b041' 32 r/1.png $A
row '#242424' '#eeeeec' '#f5b041' 32 r/2.png $B
row '#242424' '#eeeeec' '#f5b041' 16 r/3.png $A
row '#242424' '#eeeeec' '#f5b041' 16 r/4.png $B
row '#fafafa' '#2e3436' '#c37d0d' 16 r/5.png $A
row '#fafafa' '#2e3436' '#c37d0d' 16 r/6.png $B
convert -background '#808080' r/1.png r/2.png r/3.png r/4.png r/5.png r/6.png -append +repage sheet-all.png
