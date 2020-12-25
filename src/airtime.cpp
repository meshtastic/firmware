/*
We will keep hourly stats for 48 hours and daily stats for 7 days

3600 seconds in an hour, uint16_t per hour
86400 seconds in a day, uint32_t per day

Memory to store 48 hours = 96 bytes
Memory to store 7 days = 28 bytes


*/

void logTXairtime(uint32_t airtime_ms)
{
    /*

    How many hours since power up?

    Millis / 1000 / 3600



    */
}

void logRXairtime()
{
    /*

    */
}
