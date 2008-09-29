function toggle(styleIndex) {
  var rules = document.styleSheets[0].cssRules;
  if (rules == null) rules = document.styleSheets[0].rules;
  var style = rules[styleIndex].style;
  if (style.display == 'none') {
    style.display='';
  } else {
    style.display='none';
  }
}

function selectGraph(imageName, map) {
  var image = document.getElementById('relationships');
  image.setAttribute('useMap', map);
  image.setAttribute('src', imageName);
}

function syncOptions() {
  var options = document.options;
  if (options) {
    var cb = options.showRelatedCols;
    if (cb && cb.checked) {
      cb.checked=false;
      cb.click();
    }
    cb = options.showConstNames;
    if (cb && cb.checked) {
      cb.checked=false;
      cb.click();
    }
    cb = options.showComments;
    if (cb && cb.checked) {
      cb.checked=false;
      cb.click();
    }
    cb = options.showLegend;
    if (cb && !cb.checked) {
      cb.checked=true;
      cb.click();
    }
    cb = options.compact;
    if (cb && !cb.checked) {
      cb.checked=true;
      cb.click();
    }
    cb = options.implied;
    if (cb && cb.checked) {
      cb.checked=false;
      cb.click();
    }
  }
  var removeImpliedOrphans = document.getElementById('removeImpliedOrphans');
  if (removeImpliedOrphans) {
    if (removeImpliedOrphans.checked) {
      removeImpliedOrphans.checked=false;
      removeImpliedOrphans.click();
    }
  }
  syncDegrees();
}

function syncDegrees() {
  var rules = document.styleSheets[0].cssRules;
  if (rules == null) rules = document.styleSheets[0].rules;
  var degreesStyle = rules[26].style;
  var degrees = document.getElementById('degrees');
  if (degreesStyle.display != 'none' && degrees) {
    var oneDegree = document.getElementById('oneDegree');
    var twoDegrees = document.getElementById('twoDegrees');
    var useMap = document.getElementById('relationships').useMap;
    if (oneDegree.checked && useMap != '#oneDegreeRelationshipsGraph') {
      oneDegree.checked=false;
      oneDegree.click();
    } else if (twoDegrees.checked && useMap != '#twoDegreesRelationshipsGraph') {
      twoDegrees.checked=false;
      twoDegrees.click();
    }
  }
}
