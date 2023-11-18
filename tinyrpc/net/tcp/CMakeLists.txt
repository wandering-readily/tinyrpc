set(HEADERS
    abstract_slot.h  
    tcp_client.h                 
    tcp_server.h
    io_thread.h      
    tcp_connection.h
    tcp_buffer.h     
    tcp_connection_time_wheel.h

    rpc_client.h
    light_timer.h
    socket.h
)

install(FILES ${HEADERS} DESTINATION include/tinyrpc/net/tcp)