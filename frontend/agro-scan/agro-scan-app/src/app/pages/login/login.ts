import { Component } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Router } from '@angular/router';

import { Auth, signInWithEmailAndPassword } from '@angular/fire/auth';

import { FormsModule } from '@angular/forms';


@Component({
  selector: 'app-login',
  standalone: true,
  imports: [CommonModule, FormsModule],
  templateUrl: './login.html',
  styleUrls: ['./login.css']
})
export class LoginComponent {
  email = '';
  password = '';
  errorMessage = '';

  constructor(private auth: Auth, private router: Router) {}

  login() {
    this.errorMessage = '';

    signInWithEmailAndPassword(this.auth, this.email, this.password)
      .then(() => {
        // ✅ Login bem-sucedido
        this.router.navigate(['/']); // ← ajustaremos isso para ir ao Dashboard futuramente
      })
      .catch((error) => {
        console.error('Erro no login:', error);
        this.errorMessage = 'E-mail ou senha inválidos.';
      });
  }
}
