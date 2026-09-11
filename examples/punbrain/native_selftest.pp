bring core;

launch {
    if pb_core_selftest() {
        say("PunBrain native PunPun core selftest OK");
    } else {
        say("PunBrain native PunPun core selftest FAILED");
        exit(2);
    }
}
