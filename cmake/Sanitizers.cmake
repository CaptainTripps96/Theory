function(tsq_apply_sanitizers target_name)
    if(NOT TSQ_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        message(WARNING "TSQ_ENABLE_SANITIZERS is not configured for MSVC in this skeleton.")
        return()
    endif()

    target_compile_options(${target_name} PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(${target_name} PRIVATE
        -fsanitize=address,undefined
    )
endfunction()
