module.exports = function(minified) {
  var clayConfig = this;

	clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
    var health = clayConfig.getItemByMessageKey('health_info');
    var stepsToggle = clayConfig.getItemByMessageKey('steps');
    var heartSelect = clayConfig.getItemByMessageKey('heart_interval');

    function updateHealthSection() {
      if (health.get() === false) {
        stepsToggle.disable();
        heartSelect.disable();
      } else {
        stepsToggle.enable();
        heartSelect.enable();
      }
    }

    updateHealthSection();
    health.on('change', updateHealthSection);
  });
};