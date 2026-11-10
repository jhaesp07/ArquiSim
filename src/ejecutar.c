/*
 * ejecutar.c -- LA MICROARQUITECTURA: aquí se implementa lo que hace cada instrucción.
 *
 * isa.txt dice QUÉ instrucciones existen y cómo se codifican (el contrato).
 * Este archivo dice QUÉ HACE el hardware con cada una.
 *
 * Cuando se llama ejecutar(), la instrucción ya fue traída de memoria (fetch)
 * y decodificada. Usted tiene disponible:
 *
 *   in->rs, in->rt, in->rd   números de registro (0..31)
 *   in->shamt                cantidad de desplazamiento (0..31)
 *   in->imm                  inmediato de 16 bits SIN extender      (0 .. 65535)
 *   in->simm                 inmediato con extensión de signo   (-32768 .. 32767)
 *   in->direccion            campo de 26 bits de las instrucciones tipo J
 *   in->def->nombre          nombre de la instrucción en isa.txt ("add", "addi", ...)
 *
 *   cpu->reg[n]              valor del registro n
 *   cpu->acc                 acumulador (registro fuera del banco)
 *   cpu->pc                  YA apunta a la siguiente instrucción (PC + 4)
 *
 * Herramientas (ver cpu.h):
 *   escribir_registro(cpu, r, valor)      escribe un registro ($zero se ignora)
 *   leer_palabra(cpu, dir, &valor)        lee 32 bits de la RAM
 *   escribir_palabra(cpu, dir, valor)     escribe 32 bits en la RAM
 *   leer_byte / escribir_byte             acceso de 8 bits
 *   desborda_suma(a, b), desborda_resta(a, b)
 *   sumar_con_banderas(cpu, a, b)         a + b, actualizando las banderas Z N C V
 *   restar_con_banderas(cpu, a, b)        a - b, actualizando las banderas Z N C V
 *   banderas_logicas(cpu, r)              Z y N según r (C = V = 0); para and, or, ...
 *   excepcion(cpu, "mensaje", ...)        detiene el procesador con un error
 *
 * Las funciones de memoria devuelven EXCEPCION si la dirección es inválida;
 * en ese caso devuelva ese mismo resultado:
 *       if (leer_palabra(cpu, dir, &valor) != OK) return EXCEPCION;
 */
#include <string.h>
#include "cpu.h"

static int es(const Instruccion *in, const char *mnemonico)
{
    return strcmp(in->def->nombre, mnemonico) == 0;
}

Resultado ejecutar(CPU *cpu, const Instruccion *in)
{
    uint32_t valor, direccion;
    uint8_t byte;

    /* ---------------- add rd, rs, rt ---------------- */
    if (es(in, "add")) {
        uint32_t r = sumar_con_banderas(cpu, cpu->reg[in->rs], cpu->reg[in->rt]);
        if (cpu->banderas & BANDERA_V)          /* desbordamiento con signo */
            return excepcion(cpu, "desbordamiento aritmético en add");
        escribir_registro(cpu, in->rd, r);
        return OK;
    }

    /* ---------------- addi rt, rs, imm ---------------- */
    if (es(in, "addi")) {
        /* el inmediato es con signo: in->simm */
        uint32_t r = sumar_con_banderas(cpu, cpu->reg[in->rs], (uint32_t)in->simm);
        if (cpu->banderas & BANDERA_V)
            return excepcion(cpu, "desbordamiento aritmético en addi");
        escribir_registro(cpu, in->rt, r);
        return OK;
    }

    if (es(in, "sub") || es(in, "subu")) {
        uint32_t r = restar_con_banderas(cpu, cpu->reg[in->rs], cpu->reg[in->rt]);
        if (es(in, "sub") && (cpu->banderas & BANDERA_V))
            return excepcion(cpu, "desbordamiento aritmético en sub");
        escribir_registro(cpu, in->rd, r);
        return OK;
    }

    if (es(in, "addu")) {
        escribir_registro(cpu, in->rd,
                          sumar_con_banderas(cpu, cpu->reg[in->rs], cpu->reg[in->rt]));
        return OK;
    }

    if (es(in, "addiu")) {
        escribir_registro(cpu, in->rt,
                          sumar_con_banderas(cpu, cpu->reg[in->rs], (uint32_t)in->simm));
        return OK;
    }

    if (es(in, "lui")) {
        escribir_registro(cpu, in->rt, in->imm << 16);
        return OK;
    }

    if (es(in, "and") || es(in, "or") || es(in, "xor") || es(in, "nor")) {
        uint32_t a = cpu->reg[in->rs], b = cpu->reg[in->rt];
        valor = es(in, "and") ? a & b : es(in, "or") ? a | b :
                es(in, "xor") ? a ^ b : ~(a | b);
        escribir_registro(cpu, in->rd, banderas_logicas(cpu, valor));
        return OK;
    }

    if (es(in, "andi") || es(in, "ori") || es(in, "xori")) {
        uint32_t a = cpu->reg[in->rs];
        valor = es(in, "andi") ? a & in->imm : es(in, "ori") ? a | in->imm : a ^ in->imm;
        escribir_registro(cpu, in->rt, banderas_logicas(cpu, valor));
        return OK;
    }

    if (es(in, "sll") || es(in, "srl") || es(in, "sra")) {
        uint32_t fuente = cpu->reg[in->rt];
        if (es(in, "sll"))
            valor = fuente << in->shamt;
        else if (es(in, "srl"))
            valor = fuente >> in->shamt;
        else
            valor = (uint32_t)((int32_t)fuente >> in->shamt);
        escribir_registro(cpu, in->rd, banderas_logicas(cpu, valor));
        return OK;
    }

    if (es(in, "lw")) {
        direccion = cpu->reg[in->rs] + (uint32_t)in->simm;
        if (leer_palabra(cpu, direccion, &valor) != OK)
            return EXCEPCION;
        escribir_registro(cpu, in->rt, valor);
        return OK;
    }

    if (es(in, "sw")) {
        direccion = cpu->reg[in->rs] + (uint32_t)in->simm;
        return escribir_palabra(cpu, direccion, cpu->reg[in->rt]);
    }

    if (es(in, "lb") || es(in, "lbu")) {
        direccion = cpu->reg[in->rs] + (uint32_t)in->simm;
        if (leer_byte(cpu, direccion, &byte) != OK)
            return EXCEPCION;
        valor = es(in, "lb") && (byte & 0x80) ? 0xFFFFFF00u | byte : byte;
        escribir_registro(cpu, in->rt, valor);
        return OK;
    }

    if (es(in, "sb")) {
        direccion = cpu->reg[in->rs] + (uint32_t)in->simm;
        return escribir_byte(cpu, direccion, (uint8_t)cpu->reg[in->rt]);
    }

    if (es(in, "beq") || es(in, "bne")) {
        int iguales = cpu->reg[in->rs] == cpu->reg[in->rt];
        if (iguales == es(in, "beq"))
            cpu->pc += (uint32_t)in->simm << 2;
        return OK;
    }

    if (es(in, "slt") || es(in, "sltu")) {
        int menor = es(in, "slt")
                        ? (int32_t)cpu->reg[in->rs] < (int32_t)cpu->reg[in->rt]
                        : cpu->reg[in->rs] < cpu->reg[in->rt];
        escribir_registro(cpu, in->rd, (uint32_t)menor);
        return OK;
    }

    if (es(in, "slti")) {
        escribir_registro(cpu, in->rt,
                          (uint32_t)((int32_t)cpu->reg[in->rs] < in->simm));
        return OK;
    }

    if (es(in, "j") || es(in, "jal")) {
        if (es(in, "jal"))
            escribir_registro(cpu, REG_RA, cpu->pc);
        cpu->pc = (cpu->pc & 0xF0000000u) | (in->direccion << 2);
        return OK;
    }

    if (es(in, "jr")) {
        cpu->pc = cpu->reg[in->rs];
        return OK;
    }

    return excepcion(cpu, "instrucción '%s' no implementada (ver src/ejecutar.c)",
                     in->def->nombre);
}
